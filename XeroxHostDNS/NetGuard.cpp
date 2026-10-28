#include "NetGuard.h"
#include "AppState.h"
#include "Discovery.h"
#include "Notify.h"
#include "Resolver.h"
#include "Util.h"
#include <ETH.h>

static const uint32_t CONFIRM_TIMEOUT_MS = 90000;   // static IP must be confirmed within this time after link up
static const uint32_t PROBE_EVERY_MS     = 5000;
static const uint32_t SELF_PROBE_MS      = 60000;   // ARP "who has <my IP>": a device using it answers
static const uint32_t CONFLICT_HOLD_MS   = 300000;  // conflict is over when the other MAC is silent this long
static const uint32_t RENOTIFY_MS        = 600000;  // repeat the Syslog alert while the conflict lasts
static const uint32_t NAME_ASK_MS        = 30000;

// Written by the Ethernet task (netGuardOnArp), read by the loop.
static volatile bool sawSubnet = false, sawGw = false;
static volatile uint32_t chkIp = 0, chkMask = 0, chkGw = 0;   // static settings being checked (network order)
static uint8_t  myMac[6];
static bool     myMacOk = false;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t  arpMac[6];                                     // last other MAC that claimed our IP
static uint32_t arpMs = 0;

static bool     linkWas = false, confirmed = false, fallback = false;
static uint32_t upMs = 0, lastProbe = 0, lastSelfProbe = 0;
static String   reason;

struct Conflict {
  bool     active = false;
  uint8_t  mac[6];
  uint32_t ip = 0;
  String   macStr, name, via, since, lastStr;
  uint32_t lastMs = 0, notifyMs = 0, askMs = 0;
};
static Conflict cf;

void netGuardOnArp(const uint8_t* mac, uint32_t ip) {
  if (!ip) return;
  if (ip == g_myIp && myMacOk && memcmp(mac, myMac, 6) != 0) {
    taskENTER_CRITICAL(&lock);
    memcpy(arpMac, mac, 6);
    arpMs = millis();
    taskEXIT_CRITICAL(&lock);
    return;
  }
  if (!chkIp || confirmed || ip == chkIp) return;
  if (ip == chkGw) sawGw = true;
  if ((ip & chkMask) == (chkIp & chkMask)) sawSubnet = true;
}

static void goFallback(const String& why) {
  fallback = true;
  reason = why;
  chkIp = 0;
  Serial.printf("[NET] %s -> DHCP\n", why.c_str());
  addChangeLog("ESP32", why + " → เปลี่ยนไปใช้ DHCP ชั่วคราว (ค่า Static ยังเก็บไว้)");
  notifySend(4, why + " -> ใช้ DHCP ชั่วคราว");
  ETH.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0), IPAddress((uint32_t)0));
}

// ---- IP conflict ------------------------------------------------------------
// Name of the other device from what the board already knows about its MAC.
static void lookupName() {
  for (auto& h : cfg.hosts) {
    uint8_t m[6];
    if (h.mac.length() && parseMac(h.mac, m) && memcmp(m, cf.mac, 6) == 0) {
      cf.name = h.name; cf.via = "Host ในระบบ"; return;
    }
  }
  String n = discNameOfMac(cf.mac);
  if (n.length()) { cf.name = n; cf.via = "ตารางเครื่องในเครือข่าย (NetBIOS)"; }
}

static String conflictText() {
  return "IP " + ip4Str(cf.ip) + " ชนกับอุปกรณ์ MAC " + cf.macStr + (cf.name.length() ? " ชื่อ " + cf.name : String(" (ยังไม่ทราบชื่อ)"));
}

void netGuardConflictName(const String& name, const char* via) {
  if (!cf.active || !name.length() || cf.name.equalsIgnoreCase(name)) return;
  bool first = !cf.name.length();
  cf.name = name;
  cf.via = via;
  if (first) {
    addChangeLog("ESP32", "เครื่องที่ใช้ IP ซ้ำคือ " + name + " (MAC " + cf.macStr + ", จาก " + via + ")");
    notifySend(2, conflictText());
  }
}

static void conflictLoop(uint32_t now) {
  uint8_t mac[6];
  uint32_t seen;
  taskENTER_CRITICAL(&lock);
  memcpy(mac, arpMac, 6);
  seen = arpMs;
  taskEXIT_CRITICAL(&lock);
  bool claimed = seen && now - seen < CONFLICT_HOLD_MS;
  if (claimed && (!cf.active || memcmp(mac, cf.mac, 6) != 0)) {
    cf = Conflict();
    cf.active = true;
    memcpy(cf.mac, mac, 6);
    cf.ip = g_myIp;
    cf.macStr = macStr(mac);
    cf.since = nowStr();
    lookupName();
    String t = conflictText();
    Serial.printf("[NET] %s\n", t.c_str());
    addChangeLog("ESP32", t + " – ESP32 ยังใช้ IP เดิม · เข้าหน้าเว็บสำรองได้ที่ http://" + cfg.net.hostname + ".local/ (IPv6)");
    notifySend(2, t);
    cf.notifyMs = now;
  }
  if (!cf.active) return;
  if (claimed) {
    cf.lastMs = seen;
    cf.lastStr = nowStr();
    if (!cf.name.length()) lookupName();
    if (!cf.name.length() && (!cf.askMs || now - cf.askMs > NAME_ASK_MS)) {
      cf.askMs = now;
      resolverAskOwnIpName();             // the other device may answer with its name
    }
    if (now - cf.notifyMs > RENOTIFY_MS) { cf.notifyMs = now; notifySend(2, conflictText() + " (ยังชนอยู่)"); }
    return;
  }
  String t = "ไม่พบเครื่องที่ใช้ IP " + ip4Str(cf.ip) + " ซ้ำแล้ว (MAC " + cf.macStr + (cf.name.length() ? ", " + cf.name : String("")) + ")";
  addChangeLog("ESP32", t);
  notifySend(5, t);
  cf.active = false;
}

void netGuardConflictJson(JsonObject o) {
  o["active"] = cf.active;
  if (!cf.macStr.length()) return;
  o["mac"] = cf.macStr;
  o["ip"] = ip4Str(cf.ip);
  o["name"] = cf.name;
  o["via"] = cf.via;
  o["since"] = cf.since;
  o["last"] = cf.lastStr;
}

// ---- Loop -------------------------------------------------------------------
void netGuardLoop() {
  uint32_t now = millis();
  if (!myMacOk) { ETH.macAddress(myMac); myMacOk = macValid(myMac); }
  if (!ETH.linkUp() || !g_netReady) { linkWas = false; return; }

  if (!lastSelfProbe || now - lastSelfProbe >= SELF_PROBE_MS) {
    lastSelfProbe = now;
    discArpRequest(g_myIp);
  }
  conflictLoop(now);

  if (cfg.net.dhcp || !cfg.net.autoFallback || fallback) return;
  if (!linkWas) {                       // (re)plugged: check the static settings again
    uint32_t ip, mask, gw;
    if (!parseIp4(cfg.net.ip, ip) || !parseIp4(cfg.net.mask, mask) || !parseIp4(cfg.net.gw, gw)) return;
    linkWas = true;
    confirmed = sawSubnet = sawGw = false;
    chkMask = mask; chkGw = gw; chkIp = ip;
    upMs = now;
    lastProbe = 0;
  }
  if (confirmed) return;
  if (sawGw || sawSubnet) {
    confirmed = true;
    Serial.printf("[NET] static IP %s confirmed (%s)\n", cfg.net.ip.c_str(), sawGw ? "gateway" : "subnet device");
    return;
  }
  if (now - lastProbe >= PROBE_EVERY_MS) {
    lastProbe = now;
    discArpRequest(chkGw);              // the gateway answers if this is the right network
  }
  if (now - upMs > CONFIRM_TIMEOUT_MS)
    goFallback("ไม่พบ Gateway " + cfg.net.gw + " หรืออุปกรณ์ในวง " + ip4Str(chkIp & chkMask) + " ภายใน 90 วินาที (อาจย้ายไปเสียบวงอื่น)");
}

bool netGuardFallback() { return fallback; }
String netGuardReason() { return reason; }
