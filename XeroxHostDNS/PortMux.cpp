#include "PortMux.h"
#include "AppState.h"
#include "NetGuard.h"
#include <algorithm>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/tcpip.h>

// The input hook runs in the Ethernet receive task, the output hook in the lwIP task:
// the port list and the connection table are shared through one spinlock.
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

static uint16_t ports[MAX_HOSTS];       // redirected ports, sorted
static int      nPorts = 0;

struct Conn {                           // copier ip:port -> relay port it connected to
  uint32_t ip = 0;                      // network byte order
  uint16_t peer = 0, orig = 0;
  uint32_t last = 0;
};
static const int MAX_CONNS = 64;        // short SMB connections; the oldest entry is reused
static Conn conns[MAX_CONNS];

static struct netif*   hooked = nullptr;
static netif_input_fn  origInput = nullptr;
static netif_output_fn origOutput = nullptr;

static bool hasPort(uint16_t p) {       // call with the lock held
  return std::binary_search(ports, ports + nPorts, p);
}

static uint16_t rdBE(const uint8_t* p) { return p[0] << 8 | p[1]; }

// Replace a 16-bit field and patch the TCP checksum incrementally (RFC 1624).
static void rewrite(uint8_t* field, uint8_t* chk, uint16_t to) {
  uint16_t from = rdBE(field);
  uint32_t s = (uint16_t)~rdBE(chk) + (uint32_t)(uint16_t)~from + to;
  s = (s & 0xFFFF) + (s >> 16);
  s = (s & 0xFFFF) + (s >> 16);
  uint16_t c = ~s;
  field[0] = to >> 8; field[1] = to;
  chk[0] = c >> 8;    chk[1] = c;
}

// Returns the TCP header of an IPv4 packet, or null if it is not a complete unfragmented TCP header.
static uint8_t* tcpHeader(uint8_t* ip, int len) {
  if (len < 40 || (ip[0] >> 4) != 4 || ip[9] != 6) return nullptr;
  int ihl = (ip[0] & 0x0F) * 4;
  if (ihl < 20 || len < ihl + 20 || (rdBE(ip + 6) & 0x1FFF)) return nullptr;
  return ip + ihl;
}

static void remember(uint32_t ip, uint16_t peer, uint16_t orig) {   // call with the lock held
  uint32_t now = millis();
  Conn* slot = &conns[0];
  for (auto& c : conns) {
    if (c.ip == ip && c.peer == peer) { slot = &c; break; }
    if (now - c.last > now - slot->last) slot = &c;     // oldest (unused entries have last = 0)
  }
  slot->ip = ip; slot->peer = peer; slot->orig = orig; slot->last = now;
}

static uint16_t lookup(uint32_t ip, uint16_t peer) {               // call with the lock held
  for (auto& c : conns)
    if (c.ip == ip && c.peer == peer && c.orig) { c.last = millis(); return c.orig; }
  return 0;
}

// Copier -> ESP32: <relay port> becomes PORTMUX_PORT.
static err_t muxInput(struct pbuf* p, struct netif* inp) {
  uint8_t* e = (uint8_t*)p->payload;
  if (p->len >= 14 + 28 && e[12] == 0x08 && e[13] == 0x06) {     // ARP: sender MAC / IP for NetGuard
    uint32_t spa;
    memcpy(&spa, e + 28, 4);
    netGuardOnArp(e + 22, spa);
  }
  if (p->len >= 14 + 40 && e[12] == 0x08 && e[13] == 0x00) {
    uint8_t* ip = e + 14;
    uint8_t* t = tcpHeader(ip, p->len - 14);
    uint32_t dst;
    if (t) memcpy(&dst, ip + 16, 4);
    if (t && dst == ip4_addr_get_u32(netif_ip4_addr(inp))) {
      uint16_t dport = rdBE(t + 2);
      if (dport == PORTMUX_PORT) { pbuf_free(p); return ERR_OK; }   // internal port: not reachable directly
      bool redirect;
      uint32_t src;
      memcpy(&src, ip + 12, 4);
      taskENTER_CRITICAL(&lock);
      redirect = hasPort(dport);
      if (redirect) remember(src, rdBE(t), dport);
      taskEXIT_CRITICAL(&lock);
      if (redirect) rewrite(t + 2, t + 16, PORTMUX_PORT);
    }
  }
  return origInput(p, inp);
}

// ESP32 -> copier: PORTMUX_PORT becomes the relay port the copier connected to.
static err_t muxOutput(struct netif* n, struct pbuf* p, const ip4_addr_t* to) {
  uint8_t* ip = (uint8_t*)p->payload;
  uint8_t* t = tcpHeader(ip, p->len);
  if (t && rdBE(t) == PORTMUX_PORT) {
    uint32_t dst;
    memcpy(&dst, ip + 16, 4);
    taskENTER_CRITICAL(&lock);
    uint16_t orig = lookup(dst, rdBE(t + 2));
    taskEXIT_CRITICAL(&lock);
    if (orig) rewrite(t, t + 16, orig);
  }
  return origOutput(n, p, to);
}

bool portMuxBegin() {
  bool ok = false;
  LOCK_TCPIP_CORE();
  for (struct netif* n = netif_list; n; n = n->next) {
    if (ip4_addr_get_u32(netif_ip4_addr(n)) != g_myIp || !(n->flags & NETIF_FLAG_ETHARP)) continue;
    if (n != hooked) {
      if (hooked) { hooked->input = origInput; hooked->output = origOutput; }
      origInput = n->input;
      origOutput = n->output;
      n->input = muxInput;
      n->output = muxOutput;
      hooked = n;
    }
    ok = true;
    break;
  }
  UNLOCK_TCPIP_CORE();
  if (!ok) Serial.println("[RELAY] port mux: Ethernet interface not found");
  return ok;
}

void portMuxSetPorts(const std::vector<uint16_t>& list) {
  uint16_t tmp[MAX_HOSTS];
  int n = 0;
  for (uint16_t p : list)
    if (p && p != PORTMUX_PORT && n < MAX_HOSTS) tmp[n++] = p;
  std::sort(tmp, tmp + n);
  taskENTER_CRITICAL(&lock);
  memcpy(ports, tmp, n * sizeof(uint16_t));
  nPorts = n;
  taskEXIT_CRITICAL(&lock);
}

bool portMuxHas(uint16_t port) {
  if (!hooked) return false;
  taskENTER_CRITICAL(&lock);
  bool r = hasPort(port);
  taskEXIT_CRITICAL(&lock);
  return r;
}

uint16_t portMuxOrigPort(uint32_t peerIp, uint16_t peerPort) {
  taskENTER_CRITICAL(&lock);
  uint16_t r = lookup(peerIp, peerPort);
  taskEXIT_CRITICAL(&lock);
  return r;
}
