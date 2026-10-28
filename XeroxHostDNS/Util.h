#pragma once
#include <Arduino.h>
#include <deque>

struct ChangeEntry { String time, host, text; };
struct QueryEntry  { String time, client, name, type, result; };
struct RelayEntry  { String time, client, host, target, result; uint64_t bytes; uint32_t secs; bool ok; };

extern std::deque<ChangeEntry> changeLog;
extern std::deque<QueryEntry>  queryLog;
extern std::deque<RelayEntry>  relayLog;

void   addChangeLog(const String& host, const String& text);
void   addQueryLog(const String& client, const String& name, const String& type, const String& result);
void   addRelayLog(const RelayEntry& e);
String clientLabel(uint32_t ip);   // "Printer name (ip)" when the ip is a listed printer

bool   timeValid();
String timeStr(time_t t);
String nowStr();
String uptimeStr();

String ip4Str(uint32_t a);
String ip6Str(const uint8_t* a);
bool   parseIp4(const String& s, uint32_t& out);
bool   parseIp6(const String& s, uint8_t* out);
bool   inMySubnet(uint32_t ip);
bool   isApipa(uint32_t ip);
bool   isLinkLocal6(const uint8_t* a);
// MAC: accepts AA:BB:CC:DD:EE:FF, AA-BB-.., AABB.CCDD.EEFF or AABBCCDDEEFF
bool   parseMac(const String& s, uint8_t* out);
String macStr(const uint8_t* m);
bool   macValid(const uint8_t* m);   // not all-zero, not broadcast/multicast
String qtypeStr(uint16_t t);

// UDP socket helpers (lwIP BSD sockets, non-blocking)
int    openUdp(uint16_t port, bool broadcast);
bool   joinMulticast(int s, const char* group);
void   udpSend(int s, uint32_t ip, uint16_t port, const uint8_t* buf, int len);

// DNS wire-format helper: reads a (possibly compressed) name. Returns offset after the name or -1.
int    dnsReadName(const uint8_t* p, int len, int off, char* out, int outSz);
