/*
 * Xerox Host DNS Bridge for WT32-ETH01 (ESP32-ETH01 V1.4)
 * ---------------------------------------------------------------------------
 * Problem : Xerox AltaLink C8155 cannot send "Scan to SMB" to a destination
 *           given as a Windows host name, and cannot follow the PC when its
 *           IPv4/IPv6 address changes.
 * Solution: The ESP32 acts as the printer's DNS server. It continuously finds
 *           the current address of every configured PC via NetBIOS, LLMNR and
 *           mDNS and answers the printer's A/AAAA queries with it. All other
 *           queries are forwarded to the normal company DNS.
 *           Optional TCP relay: the printer sends SMB to <ESP32-IP>:<port>
 *           and the ESP32 streams it to <current IP of the PC>:445.
 *
 * Board   : "WT32-ETH01 Ethernet Module" (Arduino-ESP32 core 2.0.x or 3.x)
 * Library : ArduinoJson 7.x (Library Manager)
 * Web UI  : http://<ESP32-IP>/   (default login admin / admin)
 */
#include <Arduino.h>
#include <ETH.h>
#include <LittleFS.h>
#if ESP_ARDUINO_VERSION_MAJOR < 3
#include <WiFi.h>
#endif
#include "AppState.h"
#include "ConfigStore.h"
#include "Discovery.h"
#include "HostDns.h"
#include "LogStore.h"
#include "NetGuard.h"
#include "Notify.h"
#include "PortMux.h"
#include "PrinterMon.h"
#include "Relay.h"
#include "Resolver.h"
#include "Util.h"
#include "Verify.h"
#include "WebUI.h"

// ---- WT32-ETH01 V1.4 Ethernet (LAN8720) -------------------------------------
#define ETH_PHY_ADDR_WT32  1
#define ETH_PHY_POWER_WT32 16
#define ETH_MDC_WT32       23
#define ETH_MDIO_WT32      18
#define ETH_CLK_WT32       ETH_CLOCK_GPIO0_IN

// Hold this pin to GND while powering on (~3 s) to restore factory settings.
#define FACTORY_RESET_PIN  32   // labelled "CFG" on the WT32-ETH01

uint32_t g_myIp = 0, g_myMask = 0;
bool     g_netReady = false;

static bool ntpStarted = false;

static void onNetEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      ETH.setHostname(cfg.net.hostname.c_str());
      Serial.println("[ETH] started");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.printf("[ETH] link up %u Mbps %s duplex\n", ETH.linkSpeed(), ETH.fullDuplex() ? "full" : "half");
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("[ETH] link down");
      break;
    default:
      break;
  }
}

// v1.3.0 dropped the copier address-book import: free the flash its files used.
static void removeOldBooks() {
  File dir = LittleFS.open("/book");
  if (!dir || !dir.isDirectory()) return;
  std::vector<String> files;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) files.push_back(String("/book/") + f.name());
  dir.close();
  for (auto& p : files) LittleFS.remove(p);
  LittleFS.rmdir("/book");
}

static void checkFactoryReset() {
  pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
  delay(20);
  if (digitalRead(FACTORY_RESET_PIN) != LOW) return;
  uint32_t t = millis();
  while (digitalRead(FACTORY_RESET_PIN) == LOW) {
    if (millis() - t > 3000) {
      Serial.println("[CFG] factory reset");
      factoryReset();
      return;
    }
    delay(50);
  }
}

static void startEthernet() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  Network.onEvent(onNetEvent);
  ETH.begin(ETH_PHY_LAN8720, ETH_PHY_ADDR_WT32, ETH_MDC_WT32, ETH_MDIO_WT32, ETH_PHY_POWER_WT32, ETH_CLK_WT32);
  ETH.enableIPv6();            // link-local fe80:: address: web UI reachable even when the IPv4 is taken
#else
  WiFi.onEvent(onNetEvent);
  ETH.begin(ETH_PHY_ADDR_WT32, ETH_PHY_POWER_WT32, ETH_MDC_WT32, ETH_MDIO_WT32, ETH_PHY_LAN8720, ETH_CLK_WT32);
#endif
  if (!cfg.net.dhcp) {
    uint32_t ip, mask, gw, dns = 0;
    if (parseIp4(cfg.net.ip, ip) && parseIp4(cfg.net.mask, mask) && parseIp4(cfg.net.gw, gw)) {
      parseIp4(cfg.net.dns, dns);
      ETH.config(IPAddress(ip), IPAddress(gw), IPAddress(mask), IPAddress(dns ? dns : gw));
      Serial.printf("[ETH] static IP %s\n", cfg.net.ip.c_str());
    } else {
      Serial.println("[ETH] invalid static IP settings, falling back to DHCP");
    }
  }
}

// Network state is polled (works the same for DHCP and static IP on both cores).
static void checkNetwork() {
  uint32_t ip = ETH.linkUp() ? (uint32_t)ETH.localIP() : 0;
  if (ip == g_myIp) return;
  g_myIp = ip;
  g_myMask = (uint32_t)ETH.subnetMask();
  g_netReady = ip != 0;
  if (g_netReady) {
    Serial.printf("[ETH] IP %s mask %s gw %s dns %s\n", ip4Str(ip).c_str(), ip4Str(g_myMask).c_str(),
                  ip4Str((uint32_t)ETH.gatewayIP()).c_str(), ip4Str((uint32_t)ETH.dnsIP()).c_str());
    portMuxBegin();
    dnsBegin();
    resolverBegin();
    relayBegin();
    notifySend(6, String("ESP32 พร้อมใช้งาน v") + FW_VERSION + " IP " + ip4Str(ip) + (cfg.net.dhcp ? " (DHCP)" : netGuardFallback() ? " (DHCP สำรอง)" : " (Static)"));
    if (!ntpStarted) {
      configTzTime(cfg.time.tz.c_str(), cfg.time.ntp.c_str(), "time.google.com");
      ntpStarted = true;
    }
  } else {
    Serial.println("[ETH] no IP");
    relayStop();
    dnsStop();
    resolverStop();
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n=== Xerox Host DNS Bridge v%s ===\n", FW_VERSION);
  if (!LittleFS.begin(true)) Serial.println("[FS] LittleFS mount failed");
  checkFactoryReset();
  configLoad();
  stateLoad();
  removeOldBooks();
  logStoreBegin();
  startEthernet();
  webBegin();
}

void loop() {
  checkNetwork();
  netGuardLoop();
  if (g_netReady) {
    relayLoop();
    dnsLoop();
    resolverLoop();
    relayLoop();
    discLoop();
    verifyLoop();
    printerMonLoop();
  }
  webLoop();
  stateLoop();
  logStoreLoop();
  if (relayBusy()) yield();   // keep the relay fast while a scan is being transferred
  else delay(1);
}
