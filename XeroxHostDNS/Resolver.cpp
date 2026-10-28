#include "Resolver.h"
#include "AppState.h"
#include "ConfigStore.h"
#include "Discovery.h"
#include "HostDns.h"
#include "NetGuard.h"
#include "Util.h"
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <vector>
#include <ETH.h>

// Sockets (kept to a minimum: lwIP has only 16 and the relay needs most of them)
static int sNb = -1;    // UDP 137: NetBIOS + LLMNR queries/replies, NetBIOS registrations
static int sMd = -1;    // UDP 5353: mDNS (QU queries + multicast announcements)

static int      activeIdx   = -1;
static uint32_t activeStart = 0;
static uint16_t txid        = 0x4100;
static uint8_t  pkt[1500];

static const char* LLMNR_GROUP = "224.0.0.252";
static const char* MDNS_GROUP  = "224.0.0.251";
// On-demand mode: an address confirmed within this time is used for a copier's job without asking
// again (a job opens several connections in a few seconds; only the first one looks the PC up).
static const uint32_t ON_DEMAND_FRESH_MS = 120000;

// ---- Remote subnet sweep (unicast NetBIOS node-status) ----------------------
struct Subnet { uint32_t first, count; };     // host byte order
static std::vector<Subnet> subnets;
static String   subnetsSrc;
static int      sweepNet = -1;
static uint32_t sweepPos = 0, sweepLastBatch = 0, sweepLastStart = 0;
static bool     sweepStarted = false;

// ---------------------------------------------------------------------------
static String firstLabel(const String& n) {
  int dot = n.indexOf('.');
  return dot > 0 ? n.substring(0, dot) : n;
}

static String parseSubnets(const String& list, std::vector<Subnet>* out) {
  int start = 0;
  while (start < (int)list.length()) {
    int c = list.indexOf(',', start);
    if (c < 0) c = list.length();
    String item = list.substring(start, c);
    item.trim();
    start = c + 1;
    if (!item.length()) continue;
    int slash = item.indexOf('/');
    uint32_t ip;
    int prefix = slash > 0 ? item.substring(slash + 1).toInt() : 24;
    if (!parseIp4(slash > 0 ? item.substring(0, slash) : item, ip)) return "Subnet ไม่ถูกต้อง: " + item;
    if (prefix < 22 || prefix > 30) return "Subnet ต้องเป็น /22 ถึง /30: " + item;
    uint32_t mask = 0xffffffffUL << (32 - prefix);
    uint32_t net = ntohl(ip) & mask;
    if (out) out->push_back({net + 1, (~mask) - 1});
  }
  return "";
}

String resolverCheckSubnets(const String& list) { return parseSubnets(list, nullptr); }

// ---- Packet builders -------------------------------------------------------
static void nbEncode(uint8_t* b, int& o, const uint8_t* nb16) {
  b[o++] = 0x20;
  for (int i = 0; i < 16; i++) { b[o++] = 'A' + (nb16[i] >> 4); b[o++] = 'A' + (nb16[i] & 0x0f); }
  b[o++] = 0;
}

static int buildNbnsQuery(uint8_t* b, uint16_t id, const String& name, bool bcast) {
  memset(b, 0, 12);
  b[0] = id >> 8; b[1] = id & 0xff;
  b[2] = 0x01;                        // RD
  b[3] = bcast ? 0x10 : 0x00;         // B (broadcast)
  b[5] = 1;                           // QDCOUNT
  int o = 12;
  uint8_t nb[16];
  for (int i = 0; i < 15; i++) nb[i] = i < (int)name.length() ? toupper((unsigned char)name[i]) : ' ';
  nb[15] = 0x00;                      // workstation service
  nbEncode(b, o, nb);
  b[o++] = 0x00; b[o++] = 0x20;       // QTYPE NB
  b[o++] = 0x00; b[o++] = 0x01;       // QCLASS IN
  return o;
}

static int buildNbstatQuery(uint8_t* b, uint16_t id) {
  memset(b, 0, 12);
  b[0] = id >> 8; b[1] = id & 0xff;
  b[5] = 1;
  int o = 12;
  uint8_t nb[16] = {'*'};             // "*" followed by NULs = node status of the whole machine
  nbEncode(b, o, nb);
  b[o++] = 0x00; b[o++] = 0x21;       // QTYPE NBSTAT
  b[o++] = 0x00; b[o++] = 0x01;
  return o;
}

static int buildDnsQuery(uint8_t* b, uint16_t id, const String& name, uint16_t qtype, uint16_t qclass, bool rd) {
  memset(b, 0, 12);
  b[0] = id >> 8; b[1] = id & 0xff;
  b[2] = rd ? 0x01 : 0x00;
  b[5] = 1;
  int o = 12, start = 0;
  while (start <= (int)name.length()) {
    int dot = name.indexOf('.', start);
    if (dot < 0) dot = name.length();
    int l = dot - start;
    if (l > 0 && l < 64) {
      b[o++] = l;
      memcpy(b + o, name.c_str() + start, l);
      o += l;
    }
    start = dot + 1;
  }
  b[o++] = 0;
  b[o++] = qtype >> 8;  b[o++] = qtype & 0xff;
  b[o++] = qclass >> 8; b[o++] = qclass & 0xff;
  return o;
}

// ---- Host matching ---------------------------------------------------------
static int findHostByLabel(const char* label, int len) {
  for (size_t i = 0; i < cfg.hosts.size(); i++) {
    if (!cfg.hosts[i].enabled) continue;
    String l = firstLabel(cfg.hosts[i].name);
    if ((int)l.length() == len && strncasecmp(l.c_str(), label, len) == 0) return i;
  }
  return -1;
}

static int findHostByNetbios(const char* nb) {
  for (size_t i = 0; i < cfg.hosts.size(); i++) {
    if (!cfg.hosts[i].enabled) continue;
    String l = firstLabel(cfg.hosts[i].name);
    if (l.length() > 15) l = l.substring(0, 15);
    if (l.equalsIgnoreCase(nb)) return i;
  }
  return -1;
}

// ---- One-off lookup (Verify) -----------------------------------------------
static bool     adhocOn = false;
static String   adhocLabel;
static uint32_t adhocV4 = 0;
static String   adhocMethod;

static void adhocCheck(const char* label, int len, uint32_t ip, const char* method) {
  if (!adhocOn || !ip || ip == g_myIp) return;
  int n = adhocLabel.length();
  bool nbMatch = n > 15 && len == 15;                // NetBIOS truncates to 15 characters
  if (!((len == n || nbMatch) && strncasecmp(adhocLabel.c_str(), label, len) == 0)) return;
  if (adhocV4 && inMySubnet(adhocV4) && !inMySubnet(ip)) return;   // keep the address in our LAN
  adhocV4 = ip;
  adhocMethod = method;
}

// ---- Observations ----------------------------------------------------------
static void touch(HostState& h, const char* method) {
  h.lastSeenMs = millis();
  h.lastSeen = time(nullptr);
  h.lastSeenStr = nowStr();
  h.method = method;
  h.fails = 0;
  h.roundHits++;
}

// `weak` = learned from a DNS server (may be stale), not from the PC itself.
static void observe4(int idx, uint32_t ip, const char* method, bool weak) {
  if (!ip || ip == 0xffffffff || ip == g_myIp) return;
  HostState& h = hostState[idx];
  uint32_t now = millis();
  uint32_t fresh = cfg.res.interval * 2000UL;
  bool byMac = strstr(method, "MAC") != nullptr;
  if (h.v4 != ip) {
    if (weak && h.v4 && h.v4StrongMs && now - h.v4StrongMs < fresh) return;  // PC answered itself recently
    if (!byMac && h.v4MacMs && now - h.v4MacMs < fresh) return;              // MAC match beats a name match
    bool keepOld = h.v4 && now - h.v4SeenMs < fresh &&
                   ((inMySubnet(h.v4) && !inMySubnet(ip) && !weak && h.v4StrongMs) ||
                    (!isApipa(h.v4) && isApipa(ip)));
    if (keepOld) return;                // multi-homed host: prefer the address in our subnet
    String old = ip4Str(h.v4);
    h.v4 = ip;
    addChangeLog(cfg.hosts[idx].name, "IPv4 " + (old.length() ? old : String("(ไม่ทราบ)")) + " → " + ip4Str(ip) + " [" + method + "]");
    stateMarkDirty();
  }
  h.v4SeenMs = now;
  if (!weak) h.v4StrongMs = now;
  if (byMac) h.v4MacMs = now;
  touch(h, method);
}

static void observe6(int idx, const uint8_t* a, const char* method) {
  HostState& h = hostState[idx];
  uint32_t now = millis();
  h.roundV6Hits++;
  for (int k = 0; k < h.v6n; k++) {
    if (memcmp(h.v6[k].a, a, 16) == 0) { h.v6[k].seenMs = now; touch(h, method); return; }
  }
  int slot = h.v6n;
  if (h.v6n >= MAX_V6) {                // replace the oldest (prefer evicting link-local)
    slot = 0;
    for (int k = 1; k < h.v6n; k++) {
      bool kl = isLinkLocal6(h.v6[k].a), sl = isLinkLocal6(h.v6[slot].a);
      if ((kl && !sl) || (kl == sl && h.v6[k].seenMs < h.v6[slot].seenMs)) slot = k;
    }
  } else {
    h.v6n++;
  }
  memcpy(h.v6[slot].a, a, 16);
  h.v6[slot].seenMs = now;
  if (!isLinkLocal6(a)) {
    addChangeLog(cfg.hosts[idx].name, "IPv6 + " + ip6Str(a) + " [" + method + "]");
    stateMarkDirty();
  }
  touch(h, method);
}

// ---- Packet parsers --------------------------------------------------------
static bool nbDecode(const char* enc, char* out, uint8_t* suffix) {
  uint8_t raw[16];
  for (int i = 0; i < 16; i++) {
    int hi = toupper((unsigned char)enc[2 * i]) - 'A', lo = toupper((unsigned char)enc[2 * i + 1]) - 'A';
    if (hi < 0 || hi > 15 || lo < 0 || lo > 15) return false;
    raw[i] = (hi << 4) | lo;
  }
  int n = 15;
  while (n > 0 && (raw[n - 1] == ' ' || raw[n - 1] == 0)) n--;
  memcpy(out, raw, n);
  out[n] = 0;
  *suffix = raw[15];
  return true;
}

static void handleNbns(const uint8_t* p, int len, uint32_t srcIp) {
  if (len < 12) return;
  uint16_t flags = (p[2] << 8) | p[3];
  bool qr = flags & 0x8000;
  uint8_t op = (flags >> 11) & 0x0f, rcode = flags & 0x0f;
  if (rcode) return;
  if (qr && op != 0) return;                                        // only query responses
  if (!qr && !(cfg.res.passive && (op == 5 || op == 8 || op == 9 || op == 15))) return;  // registrations
  int qd = (p[4] << 8) | p[5];
  int rrs = ((p[6] << 8) | p[7]) + ((p[8] << 8) | p[9]) + ((p[10] << 8) | p[11]);
  int o = 12;
  char name[128];
  for (int i = 0; i < qd; i++) {
    o = dnsReadName(p, len, o, name, sizeof(name));
    if (o < 0) return;
    o += 4;
  }
  for (int i = 0; i < rrs; i++) {
    o = dnsReadName(p, len, o, name, sizeof(name));
    if (o < 0 || o + 10 > len) return;
    uint16_t type = (p[o] << 8) | p[o + 1];
    uint16_t rdlen = (p[o + 8] << 8) | p[o + 9];
    o += 10;
    if (o + rdlen > len) return;
    const uint8_t* rd = p + o;
    if (type == 0x20 && strlen(name) >= 32) {                      // NB: name -> addresses
      char nb[16]; uint8_t suffix;
      if (nbDecode(name, nb, &suffix) && (suffix == 0x00 || suffix == 0x20)) {
        int idx = findHostByNetbios(nb);
        for (int k = 0; k + 6 <= rdlen; k += 6) {
          if (((rd[k] << 8) | rd[k + 1]) & 0x8000) continue;        // group name
          uint32_t ip;
          memcpy(&ip, rd + k + 2, 4);
          adhocCheck(nb, strlen(nb), ip, "NetBIOS");
          if (idx >= 0) observe4(idx, ip, "NetBIOS", false);
        }
      }
    } else if (type == 0x21 && rdlen >= 1) {                       // NBSTAT: machine at srcIp -> its names + MAC
      int cnt = rd[0];
      char computer[16] = "";
      for (int k = 0; k < cnt && 1 + (k + 1) * 18 <= rdlen; k++) {
        const uint8_t* e = rd + 1 + k * 18;
        uint8_t suffix = e[15];
        if (((e[16] << 8) | e[17]) & 0x8000) continue;             // group name
        if (suffix != 0x00 && suffix != 0x20) continue;
        char nb[16];
        int n = 15;
        while (n > 0 && (e[n - 1] == ' ' || e[n - 1] == 0)) n--;
        memcpy(nb, e, n);
        nb[n] = 0;
        if (suffix == 0x00 && !computer[0]) strcpy(computer, nb);
        adhocCheck(nb, strlen(nb), srcIp, "NetBIOS");
        int idx = findHostByNetbios(nb);
        if (idx >= 0) observe4(idx, srcIp, "NetBIOS-Scan", false);
      }
      if (1 + cnt * 18 + 6 <= rdlen) discOnNbstat(srcIp, rd + 1 + cnt * 18, computer);  // unit ID = MAC
    }
    o += rdlen;
  }
}

enum Method { M_LLMNR, M_MDNS, M_DNS };

static void handleDnsStyle(const uint8_t* p, int len, Method m) {
  if (len < 12) return;
  uint16_t flags = (p[2] << 8) | p[3];
  if (!(flags & 0x8000) || (flags & 0x0f)) return;                  // responses without error only
  int qd = (p[4] << 8) | p[5];
  int rrs = ((p[6] << 8) | p[7]) + ((p[8] << 8) | p[9]) + ((p[10] << 8) | p[11]);
  int o = 12;
  char name[256];
  for (int i = 0; i < qd; i++) {
    o = dnsReadName(p, len, o, name, sizeof(name));
    if (o < 0) return;
    o += 4;
  }
  for (int i = 0; i < rrs; i++) {
    o = dnsReadName(p, len, o, name, sizeof(name));
    if (o < 0 || o + 10 > len) return;
    uint16_t type = (p[o] << 8) | p[o + 1];
    uint16_t cls = ((p[o + 2] << 8) | p[o + 3]) & 0x7fff;
    uint16_t rdlen = (p[o + 8] << 8) | p[o + 9];
    o += 10;
    if (o + rdlen > len) return;
    if (cls == 1 && ((type == 1 && rdlen == 4) || (type == 28 && rdlen == 16))) {
      const char* dot = strchr(name, '.');
      int l1 = dot ? dot - name : strlen(name);
      bool suffixOk = m == M_DNS || !dot || strcasecmp(dot + 1, "local") == 0;
      if (suffixOk && type == 1) {
        uint32_t ip;
        memcpy(&ip, p + o, 4);
        adhocCheck(name, l1, ip, m == M_MDNS ? "mDNS" : m == M_LLMNR ? "LLMNR" : "DNS");
      }
      int idx = suffixOk ? findHostByLabel(name, l1) : -1;
      if (idx >= 0) {
        const char* label = m == M_MDNS ? "mDNS" : m == M_LLMNR ? "LLMNR" : "DNS";
        if (type == 1) { uint32_t ip; memcpy(&ip, p + o, 4); observe4(idx, ip, label, m == M_DNS); }
        else observe6(idx, p + o, label);
      }
    }
    o += rdlen;
  }
}

void resolverOnUpstreamReply(const uint8_t* p, int len) {
  if (cfg.res.dnsLookup) handleDnsStyle(p, len, M_DNS);
}

void resolverAdhocBegin(const String& name, uint32_t unicastIp) {
  adhocOn = true;
  adhocLabel = firstLabel(name);
  adhocV4 = 0;
  adhocMethod = "";
  if (sNb < 0) return;
  uint8_t b[300];
  int n;
  String nb = adhocLabel.length() > 15 ? adhocLabel.substring(0, 15) : adhocLabel;   // NetBIOS limit
  n = buildNbnsQuery(b, txid++, nb, true);
  udpSend(sNb, g_myIp | ~g_myMask, 137, b, n);
  if (unicastIp) {
    n = buildNbnsQuery(b, txid++, nb, false);
    udpSend(sNb, unicastIp, 137, b, n);
  }
  uint32_t llmnr = inet_addr(LLMNR_GROUP);
  n = buildDnsQuery(b, txid++, adhocLabel, 1, 1, false);
  udpSend(sNb, llmnr, 5355, b, n);
  if (sMd >= 0) {
    n = buildDnsQuery(b, 0, adhocLabel + ".local", 1, 0x8001, false);
    udpSend(sMd, inet_addr(MDNS_GROUP), 5353, b, n);
  }
  if (cfg.res.dnsLookup && cfg.dns.domain.length()) {
    String fqdn = name.indexOf('.') > 0 ? name : name + "." + cfg.dns.domain;
    n = buildDnsQuery(b, txid++, fqdn, 1, 1, true);
    dnsSendUpstream(b, n);
  }
}

void resolverAdhocEnd() { adhocOn = false; }

bool resolverAdhocResult(uint32_t* v4, String* method) {
  if (v4) *v4 = adhocV4;
  if (method) *method = adhocMethod;
  return adhocV4 != 0;
}

void resolverSendNbstat(uint32_t ip) {
  uint8_t b[64];
  int n = buildNbstatQuery(b, txid++);
  udpSend(sNb, ip, 137, b, n);
}

static bool hostMac(int idx, uint8_t* mac) {
  return cfg.hosts[idx].mac.length() && parseMac(cfg.hosts[idx].mac, mac);
}

void resolverOnMac(const uint8_t* mac, uint32_t ip, const char* method) {
  uint8_t m[6];
  for (size_t i = 0; i < cfg.hosts.size() && i < hostState.size(); i++)
    if (cfg.hosts[i].enabled && hostMac(i, m) && memcmp(m, mac, 6) == 0) observe4(i, ip, method, false);
}

// ---- Own name: http://<hostname>/ (NetBIOS) and http://<hostname>.local/ (mDNS) -------
// Lets a PC open the web UI without knowing the board's IP (e.g. after a DHCP fallback).
static bool ownLinkLocal6(uint8_t* out) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_ip6_addr_t a;
  if (!ETH.netif() || esp_netif_get_ip6_linklocal(ETH.netif(), &a) != ESP_OK) return false;
  memcpy(out, a.addr, 16);
  return true;
#else
  return false;
#endif
}

static String ownLabel() {
  String h = cfg.net.hostname;
  int dot = h.indexOf('.');
  return dot > 0 ? h.substring(0, dot) : h;
}

static void answerNbns(const uint8_t* p, int len, const struct sockaddr_in& from) {
  if (len < 12 + 34 + 4 || (p[2] & 0x80) || ((p[2] >> 3) & 0x0f) != 0) return;   // name queries only
  if (((p[4] << 8) | p[5]) != 1 || p[12] != 0x20) return;
  char nb[16]; uint8_t suffix;
  if (!nbDecode((const char*)p + 13, nb, &suffix) || (suffix != 0x00 && suffix != 0x20)) return;
  String me = ownLabel();
  if (me.length() > 15) me = me.substring(0, 15);
  if (!me.equalsIgnoreCase(nb) || ((p[12 + 34] << 8) | p[12 + 35]) != 0x20) return;
  uint8_t r[12 + 34 + 16];
  memset(r, 0, sizeof(r));
  r[0] = p[0]; r[1] = p[1];
  r[2] = 0x85; r[3] = 0x00;                           // response, authoritative
  r[7] = 1;                                           // one answer
  memcpy(r + 12, p + 12, 34);                         // the queried name
  uint8_t* a = r + 12 + 34;
  a[1] = 0x20; a[3] = 0x01;                           // NB, IN
  a[6] = 0x01; a[7] = 0x2c;                           // TTL 300
  a[9] = 6;                                           // flags (unique, B-node) + IPv4
  memcpy(a + 12, &g_myIp, 4);
  udpSend(sNb, from.sin_addr.s_addr, ntohs(from.sin_port), r, sizeof(r));
}

static void answerMdns(const uint8_t* p, int len, const struct sockaddr_in& from) {
  if (len < 12 || (p[2] & 0x80)) return;              // queries only
  int qd = (p[4] << 8) | p[5], o = 12;
  String me = ownLabel() + ".local";
  char name[256];
  for (int i = 0; i < qd; i++) {
    o = dnsReadName(p, len, o, name, sizeof(name));
    if (o < 0 || o + 4 > len) return;
    uint16_t type = (p[o] << 8) | p[o + 1];
    bool unicast = p[o + 2] & 0x80;                   // QU bit
    o += 4;
    if (!me.equalsIgnoreCase(name) || (type != 1 && type != 28 && type != 255)) continue;
    String lbl = ownLabel();
    if (lbl.length() > 63) return;
    // A + AAAA (IPv6 link-local): a browser that prefers IPv6 still reaches the board when
    // another device uses its IPv4 address.
    uint8_t v6[16];
    bool has6 = ownLinkLocal6(v6);
    uint8_t r[160];
    int n = 12, an = 0;
    memset(r, 0, 12);
    bool legacy = ntohs(from.sin_port) != 5353;       // one-shot resolver: echo the id and question
    if (legacy) { r[0] = p[0]; r[1] = p[1]; }
    r[2] = 0x84;                                      // response, authoritative
    r[n++] = lbl.length(); memcpy(r + n, lbl.c_str(), lbl.length()); n += lbl.length();
    r[n++] = 5; memcpy(r + n, "local", 5); n += 5; r[n++] = 0;
    bool named = false;                               // the name at offset 12 still needs its record fields
    if (legacy) { r[5] = 1; r[n++] = 0; r[n++] = type; r[n++] = 0; r[n++] = 1; }
    else named = true;
    auto rec = [&](uint8_t t, const uint8_t* d, int dl) {
      if (!named) { r[n++] = 0xC0; r[n++] = 0x0C; }
      named = false;
      uint8_t f[] = {0x00, t, (uint8_t)(legacy ? 0x00 : 0x80), 0x01, 0, 0, 0, 120, 0, (uint8_t)dl};
      memcpy(r + n, f, sizeof(f)); n += sizeof(f);
      memcpy(r + n, d, dl); n += dl;
      an++;
    };
    rec(1, (const uint8_t*)&g_myIp, 4);
    if (has6) rec(28, v6, 16);
    r[7] = an;
    if (legacy || unicast) udpSend(sMd, from.sin_addr.s_addr, ntohs(from.sin_port), r, n);
    else udpSend(sMd, inet_addr(MDNS_GROUP), 5353, r, n);
    return;
  }
}

// ---- Packets that carry our own IPv4 but were not sent by us: another device uses it ------
// Its NetBIOS registrations / node status and mDNS answers reveal its name.
static void fromOwnIp(const uint8_t* p, int len, uint16_t sport) {
  if (len < 12) return;
  bool resp = p[2] & 0x80;
  uint8_t op = (p[2] >> 3) & 0x0f;
  int qd = (p[4] << 8) | p[5];
  int rrs = ((p[6] << 8) | p[7]) + ((p[8] << 8) | p[9]) + ((p[10] << 8) | p[11]);
  char name[256];
  int o = 12;
  if (sport == 137) {
    if (!resp && op != 5 && op != 8 && op != 9) return;           // our own queries loop back
    for (int i = 0; i < qd + rrs; i++) {
      o = dnsReadName(p, len, o, name, sizeof(name));
      if (o < 0 || o + 4 > len) return;
      uint16_t type = (p[o] << 8) | p[o + 1];
      if (i < qd) { o += 4; continue; }
      if (o + 10 > len) return;
      uint16_t rdlen = (p[o + 8] << 8) | p[o + 9];
      const uint8_t* rd = p + o + 10;
      if (o + 10 + rdlen > len) return;
      char nb[16]; uint8_t suffix;
      if (type == 0x20 && rdlen >= 6 && !(rd[0] & 0x80) &&                 // unique name (not a workgroup)
          strlen(name) >= 32 && nbDecode(name, nb, &suffix) && suffix == 0x00 && !ownLabel().equalsIgnoreCase(nb)) {
        netGuardConflictName(nb, "NetBIOS"); return;
      }
      if (type == 0x21 && rdlen >= 19) {                           // node status: first unique name
        for (int k = 0; k < rd[0] && 1 + (k + 1) * 18 <= rdlen; k++) {
          const uint8_t* e = rd + 1 + k * 18;
          if (e[15] != 0x00 || (((e[16] << 8) | e[17]) & 0x8000)) continue;
          int l = 15;
          while (l > 0 && (e[l - 1] == ' ' || e[l - 1] == 0)) l--;
          String n((const char*)e, l);
          if (n.length() && !ownLabel().equalsIgnoreCase(n)) { netGuardConflictName(n, "NetBIOS"); return; }
        }
      }
      o += 10 + rdlen;
    }
  } else if (sport == 5353 && resp) {
    for (int i = 0; i < qd + rrs; i++) {
      o = dnsReadName(p, len, o, name, sizeof(name));
      if (o < 0 || o + 4 > len) return;
      uint16_t type = (p[o] << 8) | p[o + 1];
      if (i < qd) { o += 4; continue; }
      if (o + 10 > len) return;
      uint16_t rdlen = (p[o + 8] << 8) | p[o + 9];
      int rdo = o + 10;
      if (rdo + rdlen > len) return;
      String host;
      if (type == 12) {                                            // PTR <our ip>.in-addr.arpa -> name.local
        char t[256];
        if (dnsReadName(p, len, rdo, t, sizeof(t)) > 0) host = t;
      } else if (type == 1 && rdlen == 4 && memcmp(p + rdo, &g_myIp, 4) == 0) {
        host = name;
      }
      int dot = host.indexOf('.');
      if (dot > 0) host = host.substring(0, dot);
      if (host.length() && !ownLabel().equalsIgnoreCase(host)) { netGuardConflictName(host, "mDNS"); return; }
      o = rdo + rdlen;
    }
  }
}

static void drain(int s) {
  if (s < 0) return;
  for (int i = 0; i < 16; i++) {
    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    int n = recvfrom(s, pkt, sizeof(pkt), 0, (struct sockaddr*)&from, &fl);
    if (n <= 0) break;
    if (from.sin_addr.s_addr == g_myIp) {          // our own multicast/broadcast - or another device using our IP
      fromOwnIp(pkt, n, ntohs(from.sin_port));
      continue;
    }
    uint16_t sport = ntohs(from.sin_port);
    if (sport == 5355) handleDnsStyle(pkt, n, M_LLMNR);
    else if (s == sMd || sport == 5353) { if (s == sMd) answerMdns(pkt, n, from); handleDnsStyle(pkt, n, M_MDNS); }
    else { answerNbns(pkt, n, from); handleNbns(pkt, n, from.sin_addr.s_addr); }
  }
}

// ---- Active rounds ---------------------------------------------------------
static void startRound(int idx) {
  const HostCfg& hc = cfg.hosts[idx];
  HostState& h = hostState[idx];
  String label = firstLabel(hc.name);
  uint8_t b[300];
  int n;

  h.lastQueryMs = millis();
  h.queried = true;
  h.roundHits = h.roundV6Hits = 0;
  h.priority = false;

  uint8_t mac[6];
  if (hostMac(idx, mac) && h.v4) {               // MAC host: verify the last known address
    if (inMySubnet(h.v4)) discArpProbe(h.v4);    // answered by every PC, firewall or not
    else resolverSendNbstat(h.v4);               // other LAN: node status carries the MAC
  }

  if (cfg.res.nbns && label.length() <= 15) {
    n = buildNbnsQuery(b, txid++, label, true);
    udpSend(sNb, g_myIp | ~g_myMask, 137, b, n);
    String extra = cfg.res.extraBcast;
    int start = 0;
    while (start < (int)extra.length()) {
      int c = extra.indexOf(',', start);
      if (c < 0) c = extra.length();
      uint32_t bc;
      if (parseIp4(extra.substring(start, c), bc)) udpSend(sNb, bc, 137, b, n);
      start = c + 1;
    }
    uint32_t fb = 0;
    n = buildNbnsQuery(b, txid++, label, false);
    if (h.v4) udpSend(sNb, h.v4, 137, b, n);     // unicast to last known address (works across routers)
    if (parseIp4(hc.fallback4, fb) && fb != h.v4) udpSend(sNb, fb, 137, b, n);
  }
  if (cfg.res.llmnr) {
    uint32_t grp = inet_addr(LLMNR_GROUP);
    n = buildDnsQuery(b, txid++, label, 1, 1, false);
    udpSend(sNb, grp, 5355, b, n);
    n = buildDnsQuery(b, txid++, label, 28, 1, false);
    udpSend(sNb, grp, 5355, b, n);
  }
  if (cfg.res.mdns && sMd >= 0) {
    uint32_t grp = inet_addr(MDNS_GROUP);
    String q = label + ".local";
    n = buildDnsQuery(b, 0, q, 1, 0x8001, false);   // QU bit: ask for a unicast reply
    udpSend(sMd, grp, 5353, b, n);
    n = buildDnsQuery(b, 0, q, 28, 0x8001, false);
    udpSend(sMd, grp, 5353, b, n);
  }
  if (cfg.res.dnsLookup) {
    String fqdn = hc.name.indexOf('.') > 0 ? hc.name
                : cfg.dns.domain.length() ? hc.name + "." + cfg.dns.domain : String("");
    if (fqdn.length()) {
      n = buildDnsQuery(b, txid++, fqdn, 1, 1, true);
      dnsSendUpstream(b, n);
      n = buildDnsQuery(b, txid++, fqdn, 28, 1, true);
      dnsSendUpstream(b, n);
    }
  }
}

static void finishRound(int idx) {
  HostState& h = hostState[idx];
  uint8_t mac[6], seen[6];
  if (hostMac(idx, mac) && h.v4 && inMySubnet(h.v4)) {
    if (discArpLookup(h.v4, seen) && memcmp(seen, mac, 6) == 0) observe4(idx, h.v4, "ARP (MAC)", false);
    else discRequestScan(false);                 // another device has this IP now / no answer: sweep the LAN
  } else if (hostMac(idx, mac) && !h.v4 && h.roundHits == 0) {
    discRequestScan(false);                      // never found by name: only the MAC can locate it
  }
  h.rounds++;
  if (h.roundHits == 0) h.fails++;
  if (h.roundV6Hits > 0) {                       // host answered with IPv6: drop addresses it no longer has
    uint32_t now = millis(), maxAge = cfg.res.interval * 3000UL + 5000;
    for (int k = 0; k < h.v6n;) {
      if (now - h.v6[k].seenMs > maxAge) {
        if (!isLinkLocal6(h.v6[k].a)) {
          addChangeLog(cfg.hosts[idx].name, "IPv6 - " + ip6Str(h.v6[k].a));
          stateMarkDirty();
        }
        h.v6[k] = h.v6[--h.v6n];
      } else {
        k++;
      }
    }
  }
  dnsHostResolved(idx);
}

static int pickNext() {
  uint32_t now = millis();
  int best = -1;
  uint32_t bestAge = 0;
  for (size_t i = 0; i < cfg.hosts.size(); i++) {
    if (!cfg.hosts[i].enabled) continue;
    HostState& h = hostState[i];
    if (h.priority) return i;
    uint32_t age = h.queried ? now - h.lastQueryMs : UINT32_MAX;
    if (age >= cfg.res.interval * 1000UL && (best < 0 || age > bestAge)) { best = i; bestAge = age; }
  }
  return best;
}

// Sends node-status requests to every address of the configured remote subnets,
// a few packets per call, once per interval.
static void sweepLoop() {
  if (subnetsSrc != cfg.res.scanSubnets) {
    subnetsSrc = cfg.res.scanSubnets;
    subnets.clear();
    parseSubnets(subnetsSrc, &subnets);
    sweepNet = -1;
    sweepStarted = false;
  }
  if (subnets.empty() || !cfg.res.nbns || sNb < 0) return;
  uint32_t now = millis();
  if (sweepNet < 0) {
    if (sweepStarted && now - sweepLastStart < cfg.res.interval * 1000UL) return;
    sweepStarted = true;
    sweepLastStart = now;
    sweepNet = 0;
    sweepPos = 0;
  }
  if (now - sweepLastBatch < 500) return;        // 4 packets / 500 ms = 8 per second: no scan-like burst
  sweepLastBatch = now;
  uint8_t b[64];
  int n = buildNbstatQuery(b, txid++);
  for (int i = 0; i < 4 && sweepNet >= 0; i++) {
    const Subnet& s = subnets[sweepNet];
    uint32_t ip = htonl(s.first + sweepPos);
    if (ip != g_myIp) udpSend(sNb, ip, 137, b, n);
    if (++sweepPos >= s.count) {
      sweepPos = 0;
      if (++sweepNet >= (int)subnets.size()) sweepNet = -1;
    }
  }
}

// ---- Public API ------------------------------------------------------------
void resolverStop() {
  if (sNb >= 0) close(sNb);
  if (sMd >= 0) close(sMd);
  sNb = sMd = -1;
  activeIdx = -1;
}

void resolverBegin() {
  resolverStop();
  sNb = openUdp(137, true);
  if (sNb < 0) sNb = openUdp(0, true);           // port 137 busy: still able to query
  if (cfg.res.mdns) {
    sMd = openUdp(5353, false);
    if (sMd >= 0 && !joinMulticast(sMd, MDNS_GROUP)) Serial.println("[RES] mDNS join failed");
  }
  Serial.printf("[RES] sockets: nbns/llmnr=%d mdns=%d\n", sNb, sMd);
}

void resolverLoop() {
  drain(sNb);
  drain(sMd);
  sweepLoop();

  if (activeIdx >= 0 && millis() - activeStart >= cfg.res.timeoutMs) {
    int idx = activeIdx;
    activeIdx = -1;
    if (idx < (int)hostState.size()) finishRound(idx);
  }
  if (activeIdx < 0 && sNb >= 0) {
    int idx = pickNext();
    if (idx >= 0) {
      activeIdx = idx;
      activeStart = millis();
      startRound(idx);
    }
  }
}

void resolverSeen(int idx, uint32_t ip, const char* method) {
  if (idx >= 0 && idx < (int)hostState.size()) observe4(idx, ip, method, false);
}

// (A NetBIOS node status would not help: the other device answers to "our" IP, i.e. itself.)
void resolverAskOwnIpName() {
  uint8_t b[128];
  if (sMd >= 0) {                                   // mDNS reverse lookup: <d>.<c>.<b>.<a>.in-addr.arpa PTR?
    const uint8_t* ip = (const uint8_t*)&g_myIp;
    int n = 12;
    memset(b, 0, 12);
    b[5] = 1;
    for (int i = 3; i >= 0; i--) {
      String o(ip[i]);
      b[n++] = o.length(); memcpy(b + n, o.c_str(), o.length()); n += o.length();
    }
    b[n++] = 7; memcpy(b + n, "in-addr", 7); n += 7;
    b[n++] = 4; memcpy(b + n, "arpa", 4); n += 4; b[n++] = 0;
    b[n++] = 0; b[n++] = 12; b[n++] = 0; b[n++] = 1;
    udpSend(sMd, inet_addr(MDNS_GROUP), 5353, b, n);
  }
}

void resolverPrioritize(int idx) {
  if (idx >= 0 && idx < (int)hostState.size()) hostState[idx].priority = true;
}

bool resolverFresh(int idx) {
  if (!cfg.res.onDemand) return true;
  if (idx < 0 || idx >= (int)hostState.size()) return false;
  const HostState& h = hostState[idx];
  return h.v4 && h.fails == 0 && millis() - h.v4SeenMs < ON_DEMAND_FRESH_MS;
}

void resolverRefreshAll() {
  for (auto& h : hostState) h.queried = false;
  sweepStarted = false;
}

void resolverReset() { activeIdx = -1; }
