#include "Util.h"
#include "AppState.h"
#include "LogStore.h"
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <fcntl.h>
#include <time.h>

std::deque<ChangeEntry> changeLog;
std::deque<QueryEntry>  queryLog;

static const size_t CHANGE_LOG_MAX = 60;
static const size_t QUERY_LOG_MAX  = 60;

void addChangeLog(const String& host, const String& text) {
  changeLog.push_back({nowStr(), host, text});
  while (changeLog.size() > CHANGE_LOG_MAX) changeLog.pop_front();
  logStoreAdd(LOG_CHANGE, tsvClean(host) + "\t" + tsvClean(text));
  Serial.printf("[CHANGE] %s: %s\n", host.c_str(), text.c_str());
}

void addQueryLog(const String& client, const String& name, const String& type, const String& result) {
  queryLog.push_back({nowStr(), client, name, type, result});
  while (queryLog.size() > QUERY_LOG_MAX) queryLog.pop_front();
  if (!result.startsWith("FORWARD"))   // forwarded queries are many and of little use later
    logStoreAdd(LOG_QUERY, tsvClean(client) + "\t" + tsvClean(name) + "\t" + type + "\t" + tsvClean(result));
}

std::deque<RelayEntry> relayLog;

void addRelayLog(const RelayEntry& e) {
  relayLog.push_back(e);
  while (relayLog.size() > 40) relayLog.pop_front();
  logStoreAdd(LOG_RELAY, tsvClean(e.client) + "\t" + tsvClean(e.host) + "\t" + e.target + "\t" + tsvClean(e.result) +
                         "\t" + String((uint32_t)e.bytes) + "\t" + String(e.secs) + "\t" + (e.ok ? "1" : "0"));
  Serial.printf("[RELAY] %s -> %s %s: %s\n", e.client.c_str(), e.host.c_str(), e.target.c_str(), e.result.c_str());
}

String clientLabel(uint32_t ip) {
  for (size_t i = 0; i < printerState.size() && i < cfg.printers.size(); i++)
    if (printerState[i].ip == ip) return cfg.printers[i].name + " (" + ip4Str(ip) + ")";
  return ip4Str(ip);
}

bool timeValid() { return time(nullptr) > 1700000000; }

String timeStr(time_t t) {
  if (t < 1700000000) return "";
  struct tm tmv;
  localtime_r(&t, &tmv);
  char b[24];
  strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tmv);
  return String(b);
}

String nowStr() {
  if (timeValid()) return timeStr(time(nullptr));
  return "+" + uptimeStr();
}

String uptimeStr() {
  uint32_t s = millis() / 1000;
  char b[32];
  if (s >= 86400) snprintf(b, sizeof(b), "%ud %02u:%02u:%02u", s / 86400, (s / 3600) % 24, (s / 60) % 60, s % 60);
  else snprintf(b, sizeof(b), "%02u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
  return String(b);
}

String ip4Str(uint32_t a) {
  if (!a) return "";
  const uint8_t* b = (const uint8_t*)&a;
  char s[16];
  snprintf(s, sizeof(s), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
  return String(s);
}

String ip6Str(const uint8_t* a) {
  char s[INET6_ADDRSTRLEN];
  if (!inet_ntop(AF_INET6, a, s, sizeof(s))) return "";
  return String(s);
}

bool parseIp4(const String& s, uint32_t& out) {
  String t = s; t.trim();
  if (!t.length()) return false;
  struct in_addr a;
  if (inet_pton(AF_INET, t.c_str(), &a) != 1) return false;
  out = a.s_addr;
  return true;
}

bool parseIp6(const String& s, uint8_t* out) {
  String t = s; t.trim();
  if (!t.length()) return false;
  return inet_pton(AF_INET6, t.c_str(), out) == 1;
}

bool inMySubnet(uint32_t ip) {
  return g_myIp && ((ip & g_myMask) == (g_myIp & g_myMask));
}

bool isApipa(uint32_t ip) {
  const uint8_t* b = (const uint8_t*)&ip;
  return b[0] == 169 && b[1] == 254;
}

bool isLinkLocal6(const uint8_t* a) { return a[0] == 0xfe && (a[1] & 0xc0) == 0x80; }

bool parseMac(const String& s, uint8_t* out) {
  int n = 0, nib = -1;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    int v = isdigit((unsigned char)c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
    if (v < 0) {
      if (c == ':' || c == '-' || c == '.' || c == ' ') continue;
      return false;
    }
    if (nib < 0) nib = v;
    else {
      if (n >= 6) return false;
      out[n++] = (nib << 4) | v;
      nib = -1;
    }
  }
  return n == 6 && nib < 0;
}

String macStr(const uint8_t* m) {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(b);
}

bool macValid(const uint8_t* m) {
  static const uint8_t zero[6] = {0};
  return memcmp(m, zero, 6) != 0 && !(m[0] & 0x01);
}

String qtypeStr(uint16_t t) {
  switch (t) {
    case 1: return "A";       case 2: return "NS";    case 5: return "CNAME";
    case 6: return "SOA";     case 12: return "PTR";  case 15: return "MX";
    case 16: return "TXT";    case 28: return "AAAA"; case 33: return "SRV";
    case 64: return "SVCB";   case 65: return "HTTPS"; case 255: return "ANY";
  }
  return "TYPE" + String(t);
}

int openUdp(uint16_t port, bool broadcast) {
  int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s < 0) return -1;
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  if (broadcast) setsockopt(s, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(s, (struct sockaddr*)&a, sizeof(a)) < 0) {
    Serial.printf("[UDP] bind port %u failed (errno %d)\n", port, errno);
    close(s);
    return -1;
  }
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
  uint8_t ttl = 255;
  setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
  return s;
}

bool joinMulticast(int s, const char* group) {
  struct ip_mreq m = {};
  m.imr_multiaddr.s_addr = inet_addr(group);
  m.imr_interface.s_addr = g_myIp;
  return setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof(m)) == 0;
}

void udpSend(int s, uint32_t ip, uint16_t port, const uint8_t* buf, int len) {
  if (s < 0 || !ip) return;
  struct sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = ip;
  sendto(s, buf, len, 0, (struct sockaddr*)&a, sizeof(a));
}

int dnsReadName(const uint8_t* p, int len, int off, char* out, int outSz) {
  int o = off, end = -1, jumps = 0, w = 0;
  while (true) {
    if (o >= len) return -1;
    uint8_t c = p[o];
    if (c == 0) { o++; break; }
    if ((c & 0xC0) == 0xC0) {
      if (o + 1 >= len) return -1;
      if (end < 0) end = o + 2;
      o = ((c & 0x3F) << 8) | p[o + 1];
      if (++jumps > 16) return -1;
      continue;
    }
    if (c & 0xC0) return -1;
    o++;
    if (o + c > len) return -1;
    if (w && w < outSz - 1) out[w++] = '.';
    for (int i = 0; i < c; i++) if (w < outSz - 1) out[w++] = (char)p[o + i];
    o += c;
  }
  out[w] = 0;
  return end >= 0 ? end : o;
}
