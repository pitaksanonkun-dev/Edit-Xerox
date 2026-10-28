#pragma once
#include <Arduino.h>

// Static IP safety net. Once after each link up: if the static address does not fit the network
// the board is plugged into (no gateway / no device of that subnet answers within 90 s) the board
// switches to DHCP until the next reboot (cfg.net.autoFallback). Another device using the same IP
// is only reported, unless cfg.net.conflictFallback. The static settings are always kept.
void   netGuardLoop();
void   netGuardOnArp(const uint8_t* mac, uint32_t ip);   // every received ARP sender (Ethernet task)
bool   netGuardFallback();                               // running on DHCP instead of the static IP
String netGuardReason();
String netGuardConflict();                               // MAC of a device using our static IP ("" = none)
