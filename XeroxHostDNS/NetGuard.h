#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Static IP safety net. Once after each link up: if the static address does not fit the network
// the board is plugged into (no gateway / no device of that subnet answers within 90 s) the board
// switches to DHCP until the next reboot (cfg.net.autoFallback). The static settings are kept.
//
// IP conflict watch (static or DHCP): an ARP packet from another MAC claiming the board's IPv4
// is reported (web UI, change log, Syslog) with the other device's name when it can be found.
// The board keeps its IP. The web UI stays reachable over IPv6: http://<hostname>.local/
void   netGuardLoop();
void   netGuardOnArp(const uint8_t* mac, uint32_t ip);   // every received ARP sender (Ethernet task)
bool   netGuardFallback();                               // running on DHCP instead of the static IP
String netGuardReason();
void   netGuardConflictJson(JsonObject o);               // conflict state for /api/status
void   netGuardConflictName(const String& name, const char* via);   // name heard from the other device
