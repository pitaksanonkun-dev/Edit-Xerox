#include "NetGuard.h"
#include "AppState.h"
#include "Discovery.h"
#include "Util.h"
#include <ETH.h>

static const uint32_t CONFIRM_TIMEOUT_MS = 90000;   // static IP must be confirmed within this time after link up
static const uint32_t PROBE_EVERY_MS     = 5000;

// Written by the Ethernet task (netGuardOnArp), read by the loop.
static volatile bool sawSubnet = false, sawGw = false;
static volatile uint32_t chkIp = 0, chkMask = 0, chkGw = 0;   // static settings being checked (network order)

static bool     linkWas = false, confirmed = false, fallback = false;
static uint32_t upMs = 0, lastProbe = 0;
static String   reason;

void netGuardOnArp(const uint8_t* mac, uint32_t ip) {
  (void)mac;
  if (!chkIp || confirmed || !ip || ip == chkIp) return;
  if (ip == chkGw) sawGw = true;
  if ((ip & chkMask) == (chkIp & chkMask)) sawSubnet = true;
}

static void goFallback(const String& why) {
  fallback = true;
  reason = why;
  chkIp = 0;
  Serial.printf("[NET] %s -> DHCP\n", why.c_str());
  addChangeLog("ESP32", why + " → เปลี่ยนไปใช้ DHCP ชั่วคราว (ค่า Static ยังเก็บไว้)");
  ETH.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0), IPAddress((uint32_t)0));
}

void netGuardLoop() {
  if (cfg.net.dhcp || !cfg.net.autoFallback || fallback) return;
  if (!ETH.linkUp() || !g_netReady) { linkWas = false; return; }
  uint32_t now = millis();
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
