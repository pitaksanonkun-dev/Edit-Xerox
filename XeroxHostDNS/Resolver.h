#pragma once
#include <Arduino.h>

// Discovers the current IPv4/IPv6 of every configured host name using
// NetBIOS (NBNS), LLMNR and mDNS - the same mechanisms Windows uses on a LAN.
void resolverBegin();                 // (re)open sockets - call after the network got an IP
void resolverStop();
void resolverLoop();
void resolverPrioritize(int hostIdx); // refresh this host as soon as possible
// True when the host's IPv4 was confirmed recently enough to use without asking the network
// (always true when on-demand lookups are switched off).
bool resolverFresh(int hostIdx);
void resolverRefreshAll();
void resolverReset();                 // host list changed
void resolverOnUpstreamReply(const uint8_t* p, int len);
// One-off lookup of a name that may not be saved yet (used by Verify).
void resolverAdhocBegin(const String& name, uint32_t unicastIp);
void resolverAdhocEnd();
bool resolverAdhocResult(uint32_t* v4, String* method);
void resolverSendNbstat(uint32_t ip);                              // NetBIOS node status -> names + MAC
void resolverOnMac(const uint8_t* mac, uint32_t ip, const char* method);  // device <mac> is at <ip>
// Parses "192.168.30.0/24, 10.1.2.0/23". Returns "" when valid, else an error message.
String resolverCheckSubnets(const String& list);
