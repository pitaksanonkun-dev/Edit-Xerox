#include "Verify.h"
#include "AppState.h"
#include "Discovery.h"
#include "Relay.h"
#include "Resolver.h"
#include "Util.h"
#include <ETH.h>
#include <lwip/sockets.h>
#include <fcntl.h>
#include <errno.h>
#include <vector>

static const uint32_t MAC_CONFIRM_MS  = 900;
static const uint32_t MAC_SCAN_MAX_MS = 45000;   // a /24 ARP sweep takes ~32 s at 8 requests per second
static const uint32_t NAME_WAIT_MS    = 1500;
static const uint32_t PC_TIMEOUT_MS   = 3000;
static const uint32_t PRN_TIMEOUT_MS  = 2000;

struct Step { String key, label, state, detail; };   // state: wait | run | ok | warn | fail | skip
static std::vector<Step> steps;

enum Phase { P_IDLE, P_NET, P_MAC, P_MAC_CONFIRM, P_MAC_SCAN, P_NAME, P_NAME_WAIT, P_TARGET, P_SMB, P_SMB_WAIT,
             P_PRN, P_PRN_WAIT, P_RELAY, P_DONE };
static Phase    phase = P_IDLE;
static uint32_t phaseMs = 0;
static bool     sawScan = false;

static String   vName, vAliases;
static uint8_t  vMac[6];
static bool     hasMac = false;
static uint32_t vFallback = 0, macIp = 0, nameIp = 0, target = 0;
static uint16_t vPort = 0;
static size_t   prnIdx = 0;

// ---- non-blocking TCP connect probe ----------------------------------------
static int      probeFd = -1;
static uint32_t probeStart = 0;

static bool probeBegin(uint32_t ip, uint16_t port) {
  probeFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (probeFd < 0) return false;
  fcntl(probeFd, F_SETFL, fcntl(probeFd, F_GETFL, 0) | O_NONBLOCK);
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = ip;
  probeStart = millis();
  connect(probeFd, (struct sockaddr*)&a, sizeof(a));
  return true;
}

// 1 = connected, 0 = still waiting, -1 = refused (host up, port closed), -2 = no answer / error
static int probePoll(uint32_t timeoutMs) {
  fd_set wf;
  FD_ZERO(&wf);
  FD_SET(probeFd, &wf);
  struct timeval tv = {0, 0};
  int r = 0;
  if (select(probeFd + 1, nullptr, &wf, nullptr, &tv) > 0) {
    int err = 0;
    socklen_t l = sizeof(err);
    getsockopt(probeFd, SOL_SOCKET, SO_ERROR, &err, &l);
    r = err == 0 ? 1 : err == ECONNREFUSED ? -1 : -2;
  } else if (millis() - probeStart > timeoutMs) {
    r = -2;
  }
  if (r != 0) { close(probeFd); probeFd = -1; }
  return r;
}

// ---- helpers ---------------------------------------------------------------
static Step* step(const String& key) {
  for (auto& s : steps) if (s.key == key) return &s;
  return nullptr;
}

static void set(const String& key, const char* state, const String& detail) {
  Step* s = step(key);
  if (!s) return;
  s->state = state;
  s->detail = detail;
}

static void go(Phase p) { phase = p; phaseMs = millis(); }

static void skipRest(const String& why) {
  for (auto& s : steps) if (s.state == "wait" || s.state == "run") { s.state = "skip"; s.detail = why; }
  go(P_DONE);
}

static String ms(uint32_t start) { return String(millis() - start) + " ms"; }

// ---- public ----------------------------------------------------------------
void verifyStart(const String& name, const String& aliases, const String& mac, const String& fallback4, uint16_t relayPort) {
  if (probeFd >= 0) { close(probeFd); probeFd = -1; }
  resolverAdhocEnd();
  vName = name;
  vName.trim();
  vAliases = aliases;
  hasMac = parseMac(mac, vMac) && macValid(vMac);
  vFallback = 0;
  parseIp4(fallback4, vFallback);
  vPort = relayPort;
  macIp = nameIp = target = 0;
  prnIdx = 0;
  sawScan = false;

  steps.clear();
  steps.push_back({"net", "เครือข่ายของ ESP32", "wait", ""});
  steps.push_back({"mac", "ค้นหาเครื่องจาก MAC Address (ARP)", "wait", ""});
  steps.push_back({"name", "ค้นหาเครื่องจากชื่อ (NetBIOS / LLMNR / mDNS)", "wait", ""});
  steps.push_back({"target", "IP ของเครื่องคอมพิวเตอร์ที่จะใช้", "wait", ""});
  steps.push_back({"smb", "ESP32 → คอมพิวเตอร์ : SMB (TCP 445) สำหรับรับไฟล์ Scan", "wait", ""});
  for (size_t i = 0; i < cfg.printers.size(); i++)
    steps.push_back({"p" + String(i), "ESP32 → เครื่องถ่ายเอกสาร " + cfg.printers[i].name + " (" + cfg.printers[i].ip + ")", "wait", ""});
  if (cfg.printers.empty()) steps.push_back({"p", "ESP32 → เครื่องถ่ายเอกสาร", "wait", ""});
  steps.push_back({"relay", "เครื่องถ่ายเอกสาร → ESP32 : พอร์ต Relay", "wait", ""});
  go(P_NET);
}

bool verifyRunning() { return phase != P_IDLE && phase != P_DONE; }

void verifyLoop() {
  switch (phase) {
    case P_IDLE:
    case P_DONE:
      return;

    case P_NET:
      if (!ETH.linkUp()) { set("net", "fail", "สาย LAN ไม่ได้เชื่อมต่อ"); skipRest("ข้าม – ESP32 ยังไม่ต่อเครือข่าย"); return; }
      if (!g_netReady) { set("net", "fail", "ยังไม่ได้ IP"); skipRest("ข้าม – ESP32 ยังไม่ได้ IP"); return; }
      set("net", "ok", "IP " + ip4Str(g_myIp) + " · Gateway " + ip4Str((uint32_t)ETH.gatewayIP()) + " · Link " + String(ETH.linkSpeed()) + " Mbps");
      go(P_MAC);
      return;

    case P_MAC: {
      if (!hasMac) { set("mac", "skip", "ไม่ได้ระบุ MAC – แนะนำให้ใส่เพื่อความแม่นยำสูงสุด"); go(P_NAME); return; }
      uint32_t ip = 0;
      String src;
      if (discFindMac(vMac, &ip, &src)) {
        macIp = ip;
        if (inMySubnet(ip)) {
          set("mac", "run", "พบในตาราง (" + ip4Str(ip) + ") – กำลังยืนยันด้วย ARP…");
          discArpProbe(ip);
          go(P_MAC_CONFIRM);
        } else {
          set("mac", "ok", "พบที่ " + ip4Str(ip) + " (ต่างวง LAN – จาก " + src + ")");
          go(P_NAME);
        }
        return;
      }
      set("mac", "run", "กำลังสแกนทั้งวง LAN ด้วย ARP… (ประมาณ 30 วินาที)");
      discRequestScan();
      go(P_MAC_SCAN);
      return;
    }

    case P_MAC_CONFIRM: {
      if (millis() - phaseMs < MAC_CONFIRM_MS) return;
      uint8_t seen[6];
      if (discArpLookup(macIp, seen) && memcmp(seen, vMac, 6) == 0) {
        set("mac", "ok", "พบที่ " + ip4Str(macIp) + " – ยืนยันด้วย ARP แล้ว");
        go(P_NAME);
      } else {
        macIp = 0;
        set("mac", "run", "IP เดิมไม่ใช่เครื่องนี้แล้ว – กำลังสแกนทั้งวง LAN…");
        discRequestScan();
        go(P_MAC_SCAN);
      }
      return;
    }

    case P_MAC_SCAN: {
      bool scanning = discScanning();
      if (scanning) sawScan = true;
      if ((!sawScan || scanning) && millis() - phaseMs < MAC_SCAN_MAX_MS) return;
      uint32_t ip = 0;
      String src;
      if (discFindMac(vMac, &ip, &src)) {
        macIp = ip;
        set("mac", "ok", "พบที่ " + ip4Str(ip) + " (" + src + ")");
      } else {
        set("mac", "fail", "ไม่พบ MAC นี้ในวง LAN – เครื่องปิดอยู่ / อยู่ต่างวง LAN / MAC ไม่ถูกต้อง");
      }
      go(P_NAME);
      return;
    }

    case P_NAME:
      if (!vName.length()) { set("name", "skip", "ไม่ได้ระบุชื่อ"); go(P_TARGET); return; }
      set("name", "run", "กำลังถามชื่อ " + vName + "…");
      resolverAdhocBegin(vName, macIp ? macIp : vFallback);
      go(P_NAME_WAIT);
      return;

    case P_NAME_WAIT: {
      if (millis() - phaseMs < NAME_WAIT_MS) return;
      String method;
      if (resolverAdhocResult(&nameIp, &method)) {
        set("name", "ok", "ตอบกลับจาก " + ip4Str(nameIp) + " (" + method + ")");
      } else {
        set("name", hasMac && macIp ? "warn" : "fail",
            hasMac && macIp ? "ไม่ตอบชื่อ (อาจปิด NetBIOS/LLMNR หรือ Firewall) – ไม่เป็นไร ระบบใช้ MAC แทน"
                            : "ไม่ตอบ – ตรวจชื่อเครื่อง / เปิด NetBIOS หรือใส่ MAC Address");
      }
      resolverAdhocEnd();
      go(P_TARGET);
      return;
    }

    case P_TARGET:
      target = macIp ? macIp : nameIp ? nameIp : vFallback;
      if (!target) { set("target", "fail", "ไม่ทราบ IP ของเครื่อง"); set("smb", "skip", "ข้าม – ไม่ทราบ IP"); go(P_PRN); return; }
      if (macIp && nameIp && macIp != nameIp)
        set("target", "warn", ip4Str(target) + " – ชื่อชี้ไปที่ " + ip4Str(nameIp) + " แต่ MAC อยู่ที่ " + ip4Str(macIp) + " (ระบบเชื่อ MAC)");
      else
        set("target", "ok", ip4Str(target) + (macIp ? " (จาก MAC)" : nameIp ? " (จากชื่อเครื่อง)" : " (จาก IPv4 สำรอง – ยังไม่ได้ยืนยัน)") +
                            (inMySubnet(target) ? "" : " · ต่างวง LAN"));
      if (!macIp && !nameIp) set("target", "warn", ip4Str(target) + " – ใช้ IPv4 สำรอง เพราะค้นหาเครื่องไม่พบ");
      go(P_SMB);
      return;

    case P_SMB:
      set("smb", "run", "กำลังเชื่อมต่อ " + ip4Str(target) + ":445…");
      if (!probeBegin(target, 445)) { set("smb", "fail", "socket เต็ม (มีงานส่งไฟล์อยู่) – ลองใหม่อีกครั้ง"); go(P_PRN); return; }
      go(P_SMB_WAIT);
      return;

    case P_SMB_WAIT: {
      int r = probePoll(PC_TIMEOUT_MS);
      if (r == 0) return;
      if (r == 1) set("smb", "ok", "เชื่อมต่อได้ (" + ms(probeStart) + ") – เครื่องพร้อมรับไฟล์ Scan");
      else if (r == -1) set("smb", "fail", "เครื่องตอบ แต่ปิดพอร์ต 445 – เปิด File and Printer Sharing / แชร์โฟลเดอร์ / ตรวจ Firewall");
      else set("smb", "fail", "ไม่ตอบภายใน " + String(PC_TIMEOUT_MS / 1000) + " วินาที – เครื่องปิดอยู่ หรือ Firewall / Router บล็อก");
      go(P_PRN);
      return;
    }

    case P_PRN:
      if (cfg.printers.empty()) { set("p", "warn", "ยังไม่ได้เพิ่มเครื่องถ่ายเอกสารในแท็บ เครื่องพิมพ์ Xerox"); go(P_RELAY); return; }
      if (prnIdx >= cfg.printers.size() || prnIdx >= printerState.size()) { go(P_RELAY); return; }
      set("p" + String(prnIdx), "run", "กำลังเชื่อมต่อ…");
      if (!probeBegin(printerState[prnIdx].ip, 80)) { set("p" + String(prnIdx), "fail", "socket เต็ม – ลองใหม่"); prnIdx++; return; }
      go(P_PRN_WAIT);
      return;

    case P_PRN_WAIT: {
      int r = probePoll(PRN_TIMEOUT_MS);
      if (r == 0) return;
      const PrinterState& ps = printerState[prnIdx];
      String extra = inMySubnet(ps.ip) ? "" : " · ต่างวง LAN (ผ่าน Router)";
      if (ps.dnsQueries) extra += " · เคยถาม DNS มาที่ ESP32 แล้ว " + String(ps.dnsQueries) + " ครั้ง";
      if (r == 1 || r == -1) set("p" + String(prnIdx), "ok", "ติดต่อได้ (" + ms(probeStart) + ")" + extra);
      else set("p" + String(prnIdx), "fail", "ไม่ตอบ – ตรวจว่าเครื่องเปิดอยู่ / IP ถูกต้อง / Router อนุญาต");
      prnIdx++;
      go(P_PRN);
      return;
    }

    case P_RELAY: {
      if (!vPort) { set("relay", "skip", "ไม่ได้ใช้ Relay (ใช้โหมด DNS – Address Type = Host Name)"); go(P_DONE); return; }
      for (auto& h : cfg.hosts)
        if (h.relayPort == vPort && !h.name.equalsIgnoreCase(vName)) {
          set("relay", "fail", "พอร์ต " + String(vPort) + " ถูกใช้โดย Host " + h.name + " แล้ว");
          go(P_DONE);
          return;
        }
      if (relayListening(vPort))
        set("relay", "ok", "เปิดรับอยู่ – ใน Xerox ใส่ IPv4 " + ip4Str(g_myIp) + " Port " + String(vPort));
      else
        set("relay", "warn", "ยังไม่เปิด – จะเริ่มรับหลังกด บันทึก (Xerox ใส่ " + ip4Str(g_myIp) + " Port " + String(vPort) + ")");
      go(P_DONE);
      return;
    }
  }
}

void verifyJson(JsonDocument& doc) {
  doc["running"] = verifyRunning();
  doc["started"] = phase != P_IDLE;
  int fails = 0, warns = 0;
  JsonArray arr = doc["steps"].to<JsonArray>();
  for (auto& s : steps) {
    JsonObject o = arr.add<JsonObject>();
    o["key"] = s.key;
    o["label"] = s.label;
    o["state"] = s.state;
    o["detail"] = s.detail;
    if (s.state == "fail") fails++;
    if (s.state == "warn") warns++;
  }
  doc["result"] = verifyRunning() ? "running" : fails ? "fail" : warns ? "warn" : "ok";
}
