#include "Discovery.h"
#include "AppState.h"
#include "Resolver.h"
#include "Util.h"
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>
#include <lwip/inet.h>
#include <algorithm>
#include <vector>

static const size_t   MAX_NEIGHBORS  = 512;
// 2 requests / 250 ms = 8 ARP per second: half of the 15 pps that Cisco Dynamic ARP
// Inspection allows per port before it err-disables it. A /24 sweep takes ~32 s.
static const int      ARP_BATCH      = 2;
static const uint32_t ARP_BATCH_MS   = 250;
static const uint32_t SOFT_GAP_MS    = 60000;   // routine sweep requests: at most once a minute
static const uint32_t MAX_SWEEP_HOST = 1022;    // up to a /22

struct Neighbor {
  uint8_t  mac[6];
  uint32_t ip;
  String   name;
  String   src;
  uint32_t seenMs;
  uint32_t nbstatMs;
};
static std::vector<Neighbor> nbrs;

static bool     sweeping = false, requested = false, requestedSoft = false;
static uint32_t swFirst = 0, swCount = 0, swPos = 0, swBatchMs = 0, swLastEnd = 0, swStartMs = 0;

// ---- lwIP ARP access (core locking is enabled in Arduino-ESP32) ------------
static struct netif* myNetif() {
  for (struct netif* n = netif_list; n; n = n->next)
    if (ip4_addr_get_u32(netif_ip4_addr(n)) == g_myIp) return n;
  return nullptr;
}

// lwIP keeps ARP entries up to 5 minutes even when the PC is gone, so the
// cache is flushed before a probe: everything read afterwards is a fresh reply.
// (Only 10 entries; active connections simply re-ARP within a millisecond.)
static void flushArp() {
  LOCK_TCPIP_CORE();
  struct netif* n = myNetif();
  if (n) etharp_cleanup_netif(n);
  UNLOCK_TCPIP_CORE();
}

void discArpRequest(uint32_t ip) {
  LOCK_TCPIP_CORE();
  struct netif* n = myNetif();
  if (n) {
    ip4_addr_t a;
    ip4_addr_set_u32(&a, ip);
    etharp_request(n, &a);
  }
  UNLOCK_TCPIP_CORE();
}

void discArpProbe(uint32_t ip) {
  if (!sweeping) flushArp();       // during a sweep the cache was flushed when it started
  discArpRequest(ip);
}

bool discArpLookup(uint32_t ip, uint8_t* mac) {
  bool ok = false;
  LOCK_TCPIP_CORE();
  struct netif* n = myNetif();
  if (n) {
    ip4_addr_t a;
    ip4_addr_set_u32(&a, ip);
    struct eth_addr* e = nullptr;
    const ip4_addr_t* r = nullptr;
    if (etharp_find_addr(n, &a, &e, &r) >= 0 && e) {
      memcpy(mac, e->addr, 6);
      ok = true;
    }
  }
  UNLOCK_TCPIP_CORE();
  return ok;
}

// ---- Neighbor table --------------------------------------------------------
static void update(const uint8_t* mac, uint32_t ip, const char* name, const char* src) {
  if (!ip || !macValid(mac) || ip == g_myIp) return;
  uint32_t now = millis();
  bool fromArp = strcmp(src, "ARP") == 0;
  if (fromArp)                                       // ARP is authoritative: the IP now belongs to this MAC
    nbrs.erase(std::remove_if(nbrs.begin(), nbrs.end(),
               [&](const Neighbor& n) { return n.ip == ip && memcmp(n.mac, mac, 6) != 0; }), nbrs.end());
  Neighbor* e = nullptr;
  for (auto& n : nbrs) if (memcmp(n.mac, mac, 6) == 0) e = &n;
  if (!e) {
    if (nbrs.size() >= MAX_NEIGHBORS) {
      size_t old = 0;
      for (size_t i = 1; i < nbrs.size(); i++) if (nbrs[i].seenMs < nbrs[old].seenMs) old = i;
      nbrs.erase(nbrs.begin() + old);
    }
    Neighbor n;
    memcpy(n.mac, mac, 6);
    n.ip = ip;
    n.nbstatMs = 0;
    nbrs.push_back(n);
    e = &nbrs.back();
  }
  e->ip = ip;
  e->seenMs = now;
  e->src = src;
  if (name && *name) e->name = name;
  // Ask the machine for its NetBIOS name once in a while (fills the name column).
  if (fromArp && (!e->nbstatMs || now - e->nbstatMs > 600000UL)) {
    e->nbstatMs = now;
    resolverSendNbstat(ip);
  }
  resolverOnMac(mac, ip, fromArp ? "ARP (MAC)" : "NetBIOS (MAC)");
}

bool discFindMac(const uint8_t* mac, uint32_t* ip, String* src) {
  for (auto& n : nbrs)
    if (memcmp(n.mac, mac, 6) == 0) {
      if (ip) *ip = n.ip;
      if (src) *src = n.src;
      return true;
    }
  return false;
}

bool discScanning() { return sweeping || requested; }

void discOnNbstat(uint32_t ip, const uint8_t* mac, const char* name) {
  update(mac, ip, name, "NetBIOS");
}

static void readArpTable() {
  struct Found { uint32_t ip; uint8_t mac[6]; } found[ARP_TABLE_SIZE];
  int nf = 0;
  LOCK_TCPIP_CORE();
  for (size_t i = 0; i < ARP_TABLE_SIZE; i++) {
    ip4_addr_t* ip;
    struct netif* n;
    struct eth_addr* e;
    if (etharp_get_entry(i, &ip, &n, &e) && ip && e) {
      found[nf].ip = ip4_addr_get_u32(ip);
      memcpy(found[nf].mac, e->addr, 6);
      nf++;
    }
  }
  UNLOCK_TCPIP_CORE();
  for (int i = 0; i < nf; i++)
    if (inMySubnet(found[i].ip)) update(found[i].mac, found[i].ip, nullptr, "ARP");
}

// ---- Sweep -----------------------------------------------------------------
// Sweeps only when asked: a PC with a MAC was not where we last saw it.
static bool needSweep(uint32_t now) {
  if (requested) return true;
  return requestedSoft && (!swLastEnd || now - swLastEnd >= SOFT_GAP_MS);
}

void discRequestScan(bool urgent) {
  if (urgent) requested = true;
  else requestedSoft = true;
}

void discLoop() {
  if (!g_netReady) return;
  uint32_t now = millis();
  if (!sweeping) {
    if (!needSweep(now)) return;
    uint32_t net = ntohl(g_myIp & g_myMask);
    uint32_t hosts = ~ntohl(g_myMask);
    swFirst = net + 1;
    swCount = hosts > 1 ? min(hosts - 1, MAX_SWEEP_HOST) : 0;
    swPos = 0;
    swBatchMs = 0;
    swStartMs = now;
    sweeping = true;
    requested = requestedSoft = false;
    flushArp();
  }
  if (now - swBatchMs < ARP_BATCH_MS) return;
  readArpTable();                                    // replies to the previous batch
  if (swPos >= swCount) {
    sweeping = false;
    swLastEnd = now;
    return;
  }
  for (int i = 0; i < ARP_BATCH && swPos < swCount; i++, swPos++) {
    uint32_t ip = htonl(swFirst + swPos);
    if (ip != g_myIp) discArpRequest(ip);
  }
  swBatchMs = now;
}

void discNeighborsJson(JsonDocument& doc) {
  uint32_t now = millis();
  doc["scanning"] = sweeping;
  doc["progress"] = sweeping && swCount ? (int)(swPos * 100 / swCount) : 100;
  doc["lastScan"] = swLastEnd ? (int32_t)((now - swLastEnd) / 1000) : -1;
  std::vector<const Neighbor*> list;
  for (auto& n : nbrs) list.push_back(&n);
  std::sort(list.begin(), list.end(), [](const Neighbor* a, const Neighbor* b) { return ntohl(a->ip) < ntohl(b->ip); });
  JsonArray arr = doc["list"].to<JsonArray>();
  for (auto* n : list) {
    JsonObject o = arr.add<JsonObject>();
    o["mac"] = macStr(n->mac);
    o["ip"] = ip4Str(n->ip);
    o["name"] = n->name;
    o["src"] = n->src;
    o["ago"] = (now - n->seenMs) / 1000;
    String host;
    for (auto& h : cfg.hosts) {
      uint8_t m[6];
      if (parseMac(h.mac, m) && memcmp(m, n->mac, 6) == 0) host = h.name;
    }
    o["host"] = host;
  }
}
