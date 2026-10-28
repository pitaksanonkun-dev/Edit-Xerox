#include "HostDns.h"
#include "AppState.h"
#include "Resolver.h"
#include "Util.h"
#include <ETH.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>

DnsStats dnsStats;

// One socket on UDP 53 serves clients and also talks to the upstream DNS
// (replies are told apart by the QR bit) to save lwIP sockets for the relay.
static int sDns = -1;

struct FwdEntry {
  bool used;
  uint16_t newId, origId;
  struct sockaddr_in client;
  uint32_t ts;
};
static FwdEntry fwd[24];

struct PendingQuery {
  bool used;
  int hostIdx;
  uint16_t qtype;
  int qEnd, len;
  struct sockaddr_in client;
  uint32_t deadline;
  uint8_t buf[512];
};
static PendingQuery pend[8];

static uint8_t inBuf[1500], outBuf[1500];

// ---------------------------------------------------------------------------
static void sendTo(int s, const uint8_t* b, int len, const struct sockaddr_in& to) {
  sendto(s, b, len, 0, (const struct sockaddr*)&to, sizeof(to));
}

static void sendError(const uint8_t* q, int qEnd, const struct sockaddr_in& to, uint8_t rcode) {
  int n = qEnd > 12 ? qEnd : 12;
  memcpy(outBuf, q, n);
  outBuf[2] = 0x80 | (q[2] & 0x79);        // QR + opcode + RD
  outBuf[3] = 0x80 | rcode;                // RA + rcode
  outBuf[6] = outBuf[7] = outBuf[8] = outBuf[9] = outBuf[10] = outBuf[11] = 0;
  if (qEnd <= 12) outBuf[4] = outBuf[5] = 0;
  sendTo(sDns, outBuf, n, to);
}

static int appendRR(int o, uint16_t type, const uint8_t* rd, uint16_t rdlen) {
  uint32_t ttl = cfg.dns.ttl;
  outBuf[o++] = 0xc0; outBuf[o++] = 0x0c;  // pointer to the question name
  outBuf[o++] = type >> 8; outBuf[o++] = type & 0xff;
  outBuf[o++] = 0; outBuf[o++] = 1;        // class IN
  outBuf[o++] = ttl >> 24; outBuf[o++] = ttl >> 16; outBuf[o++] = ttl >> 8; outBuf[o++] = ttl;
  outBuf[o++] = rdlen >> 8; outBuf[o++] = rdlen & 0xff;
  memcpy(outBuf + o, rd, rdlen);
  return o + rdlen;
}

static void sendLocalAnswer(const uint8_t* q, int qEnd, uint16_t qtype, int idx,
                            const struct sockaddr_in& to, const String& qname) {
  const HostCfg& hc = cfg.hosts[idx];
  HostState& h = hostState[idx];
  memcpy(outBuf, q, qEnd);
  outBuf[2] = 0x84 | (q[2] & 0x01);        // QR + AA + RD
  outBuf[3] = 0x80;                        // RA, NOERROR
  outBuf[4] = 0; outBuf[5] = 1;
  outBuf[8] = outBuf[9] = outBuf[10] = outBuf[11] = 0;
  int o = qEnd, an = 0;
  String result;

  if (qtype == 1 || qtype == 255) {
    uint32_t ip = h.v4;
    if (!ip) parseIp4(hc.fallback4, ip);
    if (ip) {
      o = appendRR(o, 1, (const uint8_t*)&ip, 4);
      an++;
      result += ip4Str(ip);
    }
  }
  if ((qtype == 28 || qtype == 255) && cfg.dns.answerAAAA) {
    int n6 = 0;
    for (int k = 0; k < h.v6n && o + 28 < (int)sizeof(outBuf) && n6 < MAX_V6; k++) {
      if (isLinkLocal6(h.v6[k].a)) continue;
      o = appendRR(o, 28, h.v6[k].a, 16);
      an++; n6++;
      result += (result.length() ? " " : "") + ip6Str(h.v6[k].a);
    }
    uint8_t fb[16];
    if (!n6 && parseIp6(hc.fallback6, fb)) {
      o = appendRR(o, 28, fb, 16);
      an++;
      result += (result.length() ? " " : "") + ip6Str(fb);
    }
  }
  outBuf[6] = an >> 8; outBuf[7] = an & 0xff;
  sendTo(sDns, outBuf, o, to);
  h.dnsHits++;
  dnsStats.local++;
  addQueryLog(clientLabel(to.sin_addr.s_addr), qname, qtypeStr(qtype),
              an ? "LOCAL → " + result : String("LOCAL (ยังไม่มีข้อมูล)"));
}

// ---- Upstream forwarding ---------------------------------------------------
static int upstreams(uint32_t* out) {
  int n = 0;
  uint32_t ip;
  if (parseIp4(cfg.dns.upstream1, ip) && ip != g_myIp) out[n++] = ip;
  if (parseIp4(cfg.dns.upstream2, ip) && ip != g_myIp) out[n++] = ip;
  if (!n) {
    ip = (uint32_t)ETH.dnsIP();
    if (ip && ip != g_myIp) out[n++] = ip;
  }
  return n;
}

static void forward(const uint8_t* q, int len, int qEnd, const struct sockaddr_in& client,
                    const String& qname, uint16_t qtype) {
  uint32_t up[3];
  int nUp = upstreams(up);
  if (!nUp) {
    sendError(q, qEnd, client, 2);         // SERVFAIL
    dnsStats.failed++;
    addQueryLog(clientLabel(client.sin_addr.s_addr), qname, qtypeStr(qtype), "SERVFAIL (ไม่มี upstream DNS)");
    return;
  }
  int slot = 0;
  for (int i = 0; i < (int)(sizeof(fwd) / sizeof(fwd[0])); i++) {
    if (!fwd[i].used) { slot = i; break; }
    if (fwd[i].ts < fwd[slot].ts) slot = i;
  }
  FwdEntry& e = fwd[slot];
  e.used = true;
  e.origId = (q[0] << 8) | q[1];
  e.newId = (uint16_t)esp_random();
  e.client = client;
  e.ts = millis();
  memcpy(outBuf, q, len);
  outBuf[0] = e.newId >> 8; outBuf[1] = e.newId & 0xff;
  for (int i = 0; i < nUp; i++) udpSend(sDns, up[i], 53, outBuf, len);
  dnsStats.forwarded++;
  addQueryLog(clientLabel(client.sin_addr.s_addr), qname, qtypeStr(qtype), "FORWARD → " + ip4Str(up[0]));
}

static void handleUpstream(int n, const struct sockaddr_in& from) {
  if (n < 12 || ntohs(from.sin_port) != 53) return;
  uint16_t id = (inBuf[0] << 8) | inBuf[1];
  for (auto& e : fwd) {
    if (!e.used || e.newId != id) continue;
    inBuf[0] = e.origId >> 8; inBuf[1] = e.origId & 0xff;
    sendTo(sDns, inBuf, n, e.client);
    e.used = false;                        // first answer wins (when 2 upstreams are set)
    return;
  }
  resolverOnUpstreamReply(inBuf, n);       // reply to the resolver's own AD/DNS lookup
}

void dnsSendUpstream(const uint8_t* buf, int len) {
  uint32_t up[3];
  int nUp = upstreams(up);
  for (int i = 0; i < nUp; i++) udpSend(sDns, up[i], 53, buf, len);
}

// ---- Client queries --------------------------------------------------------
int dnsMatchHost(const String& qname) {
  int dot = qname.indexOf('.');
  String first = dot > 0 ? qname.substring(0, dot) : qname;
  String rest = dot > 0 ? qname.substring(dot + 1) : "";
  String domain = cfg.dns.domain;
  domain.toLowerCase();
  for (size_t i = 0; i < cfg.hosts.size(); i++) {
    const HostCfg& h = cfg.hosts[i];
    if (!h.enabled) continue;
    String names = h.name + "," + h.aliases;
    int start = 0;
    while (start < (int)names.length()) {
      int c = names.indexOf(',', start);
      if (c < 0) c = names.length();
      String n = names.substring(start, c);
      n.trim();
      n.toLowerCase();
      start = c + 1;
      if (!n.length()) continue;
      if (qname == n) return i;
      if (n.indexOf('.') < 0 && first == n) {
        if (!rest.length() || rest == "local" || (domain.length() && rest == domain) || cfg.dns.anySuffix) return i;
      }
    }
  }
  return -1;
}

static void handleQuery(const uint8_t* q, int len, const struct sockaddr_in& client) {
  if (len < 12 || (q[2] & 0x80)) return;   // too short or a response
  dnsStats.queries++;
  uint32_t cip = client.sin_addr.s_addr;
  for (size_t i = 0; i < printerState.size(); i++) {
    if (printerState[i].ip != cip) continue;
    printerState[i].dnsQueries++;
    printerState[i].lastQueryTime = nowStr();
    printerState[i].online = true;
  }
  uint8_t opcode = (q[2] >> 3) & 0x0f;
  int qd = (q[4] << 8) | q[5];
  if (opcode != 0) { sendError(q, 12, client, 4); return; }   // NOTIMP
  if (qd != 1) { sendError(q, 12, client, 1); return; }       // FORMERR

  char nameBuf[256];
  int o = dnsReadName(q, len, 12, nameBuf, sizeof(nameBuf));
  if (o < 0 || o + 4 > len) { sendError(q, 12, client, 1); return; }
  uint16_t qtype = (q[o] << 8) | q[o + 1];
  int qEnd = o + 4;
  String qname = nameBuf;
  qname.toLowerCase();

  for (size_t i = 0; i < printerState.size(); i++)
    if (printerState[i].ip == cip) printerState[i].lastQuery = qname + " (" + qtypeStr(qtype) + ")";

  if (cfg.dns.restrict) {
    bool allowed = false;
    for (auto& p : printerState) if (p.ip == cip) allowed = true;
    if (!allowed) {
      sendError(q, qEnd, client, 5);       // REFUSED
      dnsStats.refused++;
      addQueryLog(ip4Str(cip), qname, qtypeStr(qtype), "REFUSED (ไม่อยู่ในรายการเครื่องพิมพ์)");
      return;
    }
  }

  int idx = dnsMatchHost(qname);
  if (idx < 0) { forward(q, len, qEnd, client, qname, qtype); return; }

  HostState& h = hostState[idx];
  bool addrType = qtype == 1 || qtype == 28 || qtype == 255;
  // No fresh data (just booted, host stopped answering, or in on-demand mode not confirmed
  // lately): hold the query until the resolver finished a new round for this host.
  if (addrType && (h.rounds == 0 || h.fails > 0 || !resolverFresh(idx))) {
    for (auto& p : pend) {
      if (p.used || len > (int)sizeof(p.buf)) continue;
      p.used = true;
      p.hostIdx = idx;
      p.qtype = qtype;
      p.qEnd = qEnd;
      p.len = len;
      p.client = client;
      p.deadline = millis() + cfg.res.timeoutMs * 2 + 400;
      memcpy(p.buf, q, len);
      resolverPrioritize(idx);
      return;
    }
  }
  if (h.fails > 0) resolverPrioritize(idx);
  sendLocalAnswer(q, qEnd, qtype, idx, client, qname);
}

static void answerPending(PendingQuery& p) {
  p.used = false;
  if (p.hostIdx >= (int)cfg.hosts.size()) return;
  char nameBuf[256];
  dnsReadName(p.buf, p.len, 12, nameBuf, sizeof(nameBuf));
  String qname = nameBuf;
  qname.toLowerCase();
  sendLocalAnswer(p.buf, p.qEnd, p.qtype, p.hostIdx, p.client, qname);
}

// ---- Public API ------------------------------------------------------------
void dnsHostResolved(int idx) {
  for (auto& p : pend) if (p.used && p.hostIdx == idx) answerPending(p);
}

void dnsReset() {
  for (auto& p : pend) p.used = false;
}

void dnsStop() {
  if (sDns >= 0) close(sDns);
  sDns = -1;
  dnsReset();
  for (auto& e : fwd) e.used = false;
}

void dnsBegin() {
  dnsStop();
  sDns = openUdp(53, false);
  Serial.printf("[DNS] server socket=%d\n", sDns);
}

void dnsLoop() {
  if (sDns < 0) return;
  for (int i = 0; i < 8; i++) {
    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    int n = recvfrom(sDns, inBuf, sizeof(inBuf), 0, (struct sockaddr*)&from, &fl);
    if (n <= 0) break;
    if (n >= 12 && (inBuf[2] & 0x80)) handleUpstream(n, from);
    else handleQuery(inBuf, n, from);
  }

  uint32_t now = millis();
  for (auto& p : pend)
    if (p.used && (int32_t)(now - p.deadline) >= 0) answerPending(p);
  for (auto& e : fwd)
    if (e.used && now - e.ts > 5000) e.used = false;
}
