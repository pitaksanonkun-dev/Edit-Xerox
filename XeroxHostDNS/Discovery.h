#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Finds every device on the ESP32's own LAN by ARP (cannot be blocked by the
// Windows firewall) and keeps a MAC -> IP / name table. Hosts configured with a
// MAC address are located through this table.
void discLoop();
// Full ARP sweep of the LAN. urgent: now (a copier is waiting / user action);
// otherwise at most once a minute. There is no periodic sweep.
void discRequestScan(bool urgent = true);
void discArpRequest(uint32_t ip);               // ask who has <ip>
void discArpProbe(uint32_t ip);                 // flush stale ARP entries, then ask who has <ip>
bool discArpLookup(uint32_t ip, uint8_t* mac);  // MAC currently cached for <ip>
bool discFindMac(const uint8_t* mac, uint32_t* ip, String* src);   // from the neighbor table
bool discScanning();
void discOnNbstat(uint32_t ip, const uint8_t* mac, const char* name);
void discNeighborsJson(JsonDocument& doc);
