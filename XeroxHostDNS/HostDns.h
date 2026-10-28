#pragma once
#include <Arduino.h>

// Small DNS server (UDP 53). Answers A/AAAA for configured hosts from the
// resolver cache and forwards every other query to the upstream DNS.
struct DnsStats { uint32_t queries = 0, local = 0, forwarded = 0, refused = 0, failed = 0; };
extern DnsStats dnsStats;

void dnsBegin();
void dnsStop();
void dnsLoop();
void dnsHostResolved(int hostIdx);   // called by the resolver when a round for a host finished
void dnsReset();                     // host list changed: drop waiting queries
int  dnsMatchHost(const String& qnameLower);
void dnsSendUpstream(const uint8_t* buf, int len);   // used by the resolver's DNS lookup method
