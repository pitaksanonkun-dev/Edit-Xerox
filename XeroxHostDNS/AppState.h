#pragma once
// ---------------------------------------------------------------------------
//  Xerox Host DNS Bridge - shared configuration & runtime state
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <vector>

#define FW_VERSION      "1.6.0"
#define MAX_HOSTS       100
#define MAX_PRINTERS    32
#define MAX_V6          4
// lwIP in Arduino-ESP32 has 16 sockets in total. Base usage: DNS 1, resolver 2, web 2,
// printer check 1, verify probe 1, short UDP lookups 1 = 8. Relay: 1 listener for all
// relay ports (PortMux) + 2 sockets per session = 7.
#define MAX_RELAY_PORTS     MAX_HOSTS
#define MAX_RELAY_SESSIONS  3
#define RELAY_TARGET_PORT   445

// ---- Persistent configuration (stored in /config.json on LittleFS) --------
struct NetCfg {
  bool   dhcp = true;
  String ip, mask, gw, dns;
  bool   autoFallback = true;          // static IP does not fit the network (moved) -> DHCP until reboot
  bool   conflictFallback = false;     // another device uses our static IP -> DHCP (off: keep the IP, only warn)
  String hostname = "xerox-dns";
};

struct DnsCfg {
  String   upstream1;              // blank = use DNS from DHCP
  String   upstream2;
  uint32_t ttl        = 30;        // TTL (seconds) of locally answered records
  String   domain;                 // domain suffix the printer appends (optional)
  bool     anySuffix  = true;      // "pc01.<anything>" also matches host "pc01"
  bool     answerAAAA = true;      // answer IPv6 (AAAA) queries
  bool     restrict   = false;     // only serve printers listed in the UI
};

struct ResolverCfg {
  bool     onDemand  = true;       // look a PC up when a copier sends to it (relay / DNS query)
  uint32_t interval  = 1800;       // seconds between background refreshes of each host (30 min)
  uint32_t timeoutMs = 800;        // wait time for replies per round
  bool     nbns      = true;       // NetBIOS Name Service (UDP 137)
  bool     llmnr     = true;       // LLMNR (UDP 5355)
  bool     mdns      = true;       // mDNS (UDP 5353, <name>.local)
  bool     passive   = true;       // listen for NetBIOS registrations / mDNS announcements
  bool     dnsLookup = false;      // also ask the upstream (company/AD) DNS for <name>.<domain>
  String   extraBcast;             // extra broadcast addresses, comma separated
  String   scanSubnets;            // other LANs to sweep with unicast NetBIOS, e.g. "192.168.30.0/24"
};

struct TimeCfg {
  String tz  = "<+07>-7";          // POSIX TZ (Bangkok)
  String ntp = "pool.ntp.org";
};

// Log retention on LittleFS (1.375 MB partition on the 4 MB WT32-ETH01).
// Daily files /logs/<type><YYYYMMDD>.tsv, written in batches to spare the flash.
struct LogCfg {
  uint16_t changeDays   = 90;      // IP change history
  uint16_t relayDays    = 30;      // scan jobs through the relay
  uint16_t queryDays    = 3;       // DNS queries (only when queryPersist)
  bool     queryPersist = false;   // DNS queries are many: RAM only by default
  uint16_t maxKB        = 512;     // total size of all log files
  uint16_t flushMin     = 5;       // write the RAM buffer to flash every N minutes
};

struct AuthCfg {
  String user = "admin";
  String pass = "admin";
};

struct PrinterCfg {
  String name, ip, model, location, note;
};

struct HostCfg {
  String name;                     // Windows computer name (scan destination)
  String aliases;                  // other names the printer may use, comma separated
  String fallback4, fallback6;     // used only when never discovered
  String note;
  bool   enabled = true;
  uint16_t relayPort = 0;          // ESP32 TCP port relayed to <host>:445 (0 = off)
  String mac;                      // optional "AA:BB:CC:DD:EE:FF": track the PC by MAC (ARP)
  String scanName;                 // Scan destination nickname(s) in the copiers, e.g. "test (Xerox ชั้น 2)"
};

struct AppConfig {
  NetCfg      net;
  DnsCfg      dns;
  ResolverCfg res;
  TimeCfg     time;
  LogCfg      log;
  AuthCfg     auth;
  std::vector<PrinterCfg> printers;
  std::vector<HostCfg>    hosts;
};

// ---- Runtime state ---------------------------------------------------------
struct V6Entry {
  uint8_t  a[16];
  uint32_t seenMs;
};

struct HostState {
  uint32_t v4       = 0;           // network byte order (in_addr.s_addr)
  uint32_t v4SeenMs = 0;
  uint32_t v4StrongMs = 0;         // last time v4 was confirmed by the PC itself (not by DNS)
  uint32_t v4MacMs = 0;            // last time v4 was confirmed by the host's MAC address
  V6Entry  v6[MAX_V6];
  uint8_t  v6n      = 0;
  time_t   lastSeen = 0;           // epoch (0 = unknown / no NTP)
  uint32_t lastSeenMs = 0;
  String   lastSeenStr;
  String   method;                 // protocol that answered last
  uint32_t lastQueryMs = 0;
  bool     queried  = false;
  uint32_t rounds   = 0;           // completed active rounds since boot
  uint16_t fails    = 0;           // consecutive rounds with no answer
  uint16_t roundHits = 0, roundV6Hits = 0;
  bool     priority = false;
  uint32_t dnsHits  = 0;
  uint32_t relayJobs = 0, relayFails = 0;
  uint64_t relayBytes = 0;
  String   relayLast;
};

struct PrinterState {
  uint32_t ip = 0;
  bool     online  = false;
  bool     checked = false;
  uint32_t lastCheckMs = 0;
  uint32_t dnsQueries  = 0;
  String   lastQuery, lastQueryTime;
};

extern AppConfig                 cfg;
extern std::vector<HostState>    hostState;     // parallel to cfg.hosts
extern std::vector<PrinterState> printerState;  // parallel to cfg.printers

extern uint32_t g_myIp, g_myMask;               // network byte order
extern bool     g_netReady;
