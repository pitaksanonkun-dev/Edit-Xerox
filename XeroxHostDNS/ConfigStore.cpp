#include "ConfigStore.h"
#include "PortMux.h"
#include "Util.h"
#include <LittleFS.h>

AppConfig                 cfg;
std::vector<HostState>    hostState;
std::vector<PrinterState> printerState;

static const char* CONFIG_FILE = "/config.json";
static const char* STATE_FILE  = "/state.json";

static bool     stateDirty   = false;
static uint32_t stateDirtyMs = 0;

// ---- JSON helpers ----------------------------------------------------------
static String jstr(JsonVariantConst v, const String& def) {
  if (v.is<const char*>()) { String s = v.as<const char*>(); s.trim(); return s; }
  return def;
}
static bool jbool(JsonVariantConst v, bool def) { return v.is<bool>() ? v.as<bool>() : def; }
static uint32_t juint(JsonVariantConst v, uint32_t def) {
  if (v.is<uint32_t>()) return v.as<uint32_t>();
  if (v.is<const char*>()) { long n = atol(v.as<const char*>()); return n > 0 ? (uint32_t)n : def; }
  return def;
}

// ---- Hosts / printers ------------------------------------------------------
void hostsFromJson(JsonArrayConst a, std::vector<HostCfg>& out) {
  out.clear();
  for (JsonVariantConst v : a) {
    HostCfg h;
    h.name      = jstr(v["name"], "");
    h.aliases   = jstr(v["aliases"], "");
    h.fallback4 = jstr(v["fallback4"], "");
    h.fallback6 = jstr(v["fallback6"], "");
    h.note      = jstr(v["note"], "");
    h.enabled   = jbool(v["enabled"], true);
    uint32_t rp = juint(v["relayPort"], 0);
    h.relayPort = rp <= 65535 ? rp : 0;
    h.scanName  = jstr(v["scanName"], "");
    h.share     = jstr(v["share"], "");
    h.docPath   = jstr(v["docPath"], "");
    h.login     = jstr(v["login"], "");
    h.mac       = jstr(v["mac"], "");
    uint8_t m[6];
    if (parseMac(h.mac, m)) h.mac = macStr(m);   // normalise to AA:BB:CC:DD:EE:FF
    out.push_back(h);
  }
}

void printersFromJson(JsonArrayConst a, std::vector<PrinterCfg>& out) {
  out.clear();
  for (JsonVariantConst v : a) {
    PrinterCfg p;
    p.name     = jstr(v["name"], "");
    p.ip       = jstr(v["ip"], "");
    p.model    = jstr(v["model"], "");
    p.location = jstr(v["location"], "");
    p.note     = jstr(v["note"], "");
    out.push_back(p);
  }
}

static bool validHostName(const String& n) {
  if (!n.length() || n.length() > 63) return false;
  for (size_t i = 0; i < n.length(); i++) {
    char c = n[i];
    if (!(isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')) return false;
  }
  return n[0] != '.' && n[n.length() - 1] != '.';
}

String validateHosts(const std::vector<HostCfg>& list) {
  if (list.size() > MAX_HOSTS) return "จำนวน Host เกิน " + String(MAX_HOSTS);
  for (size_t i = 0; i < list.size(); i++) {
    const HostCfg& h = list[i];
    if (h.relayPort) {
      uint16_t p = h.relayPort;
      if (p == 53 || p == 80 || p == 137 || p == 5353 || p == 5355 || p == PORTMUX_PORT)
        return "พอร์ต Relay " + String(p) + " ใช้ไม่ได้ (ถูกใช้โดยระบบ)";
      for (size_t j = 0; j < i; j++)
        if (list[j].relayPort == p) return "พอร์ต Relay ซ้ำ: " + String(p);
    }
    if (!validHostName(h.name)) return "ชื่อ Host ไม่ถูกต้อง: '" + h.name + "' (ใช้ได้เฉพาะ A-Z 0-9 - _ .)";
    for (size_t j = 0; j < i; j++)
      if (list[j].name.equalsIgnoreCase(h.name)) return "ชื่อ Host ซ้ำ: " + h.name;
    if (h.scanName.length() > 200) return "ชื่อ Scan ยาวเกิน 200 ตัวอักษร: " + h.name;
    if (h.share.length() > 80 || h.docPath.length() > 200 || h.login.length() > 64) return "Share / Path / Login ยาวเกินไป: " + h.name;
    uint32_t v4; uint8_t v6[16], mac[6];
    if (h.mac.length()) {
      if (!parseMac(h.mac, mac) || !macValid(mac)) return "MAC Address ไม่ถูกต้อง: " + h.mac;
      for (size_t j = 0; j < i; j++)
        if (list[j].mac.equalsIgnoreCase(h.mac)) return "MAC Address ซ้ำ: " + h.mac;
    }
    if (h.fallback4.length() && !parseIp4(h.fallback4, v4)) return "IPv4 สำรองไม่ถูกต้อง: " + h.fallback4;
    if (h.fallback6.length() && !parseIp6(h.fallback6, v6)) return "IPv6 สำรองไม่ถูกต้อง: " + h.fallback6;
  }
  return "";
}

String validatePrinters(const std::vector<PrinterCfg>& list) {
  if (list.size() > MAX_PRINTERS) return "จำนวนเครื่องพิมพ์เกิน " + String(MAX_PRINTERS);
  for (size_t i = 0; i < list.size(); i++) {
    const PrinterCfg& p = list[i];
    if (!p.name.length()) return "กรุณาระบุชื่อเครื่องพิมพ์";
    uint32_t v4;
    if (!parseIp4(p.ip, v4)) return "IP ของเครื่องพิมพ์ไม่ถูกต้อง: " + p.ip;
    for (size_t j = 0; j < i; j++)
      if (list[j].ip == p.ip) return "IP เครื่องพิมพ์ซ้ำ: " + p.ip;
  }
  return "";
}

void setHosts(const std::vector<HostCfg>& list) {
  std::vector<HostState> ns(list.size());
  for (size_t i = 0; i < list.size(); i++)
    for (size_t j = 0; j < cfg.hosts.size() && j < hostState.size(); j++)
      if (cfg.hosts[j].name.equalsIgnoreCase(list[i].name)) { ns[i] = hostState[j]; break; }
  cfg.hosts = list;
  hostState = ns;
}

void setPrinters(const std::vector<PrinterCfg>& list) {
  std::vector<PrinterState> ns(list.size());
  for (size_t i = 0; i < list.size(); i++) {
    for (size_t j = 0; j < cfg.printers.size() && j < printerState.size(); j++)
      if (cfg.printers[j].ip == list[i].ip) { ns[i] = printerState[j]; break; }
    uint32_t ip = 0;
    parseIp4(list[i].ip, ip);
    ns[i].ip = ip;
  }
  cfg.printers = list;
  printerState = ns;
}

// ---- Whole configuration ---------------------------------------------------
void configToJson(JsonDocument& doc, bool withSecrets) {
  JsonObject n = doc["net"].to<JsonObject>();
  n["dhcp"] = cfg.net.dhcp; n["ip"] = cfg.net.ip; n["mask"] = cfg.net.mask;
  n["gw"] = cfg.net.gw; n["dns"] = cfg.net.dns; n["hostname"] = cfg.net.hostname;
  n["autoFallback"] = cfg.net.autoFallback;

  JsonObject d = doc["dns"].to<JsonObject>();
  d["upstream1"] = cfg.dns.upstream1; d["upstream2"] = cfg.dns.upstream2;
  d["ttl"] = cfg.dns.ttl; d["domain"] = cfg.dns.domain; d["anySuffix"] = cfg.dns.anySuffix;
  d["answerAAAA"] = cfg.dns.answerAAAA; d["restrict"] = cfg.dns.restrict;

  JsonObject r = doc["res"].to<JsonObject>();
  r["onDemand"] = cfg.res.onDemand;
  r["interval"] = cfg.res.interval; r["timeoutMs"] = cfg.res.timeoutMs;
  r["nbns"] = cfg.res.nbns; r["llmnr"] = cfg.res.llmnr; r["mdns"] = cfg.res.mdns;
  r["passive"] = cfg.res.passive; r["extraBcast"] = cfg.res.extraBcast;
  r["dnsLookup"] = cfg.res.dnsLookup; r["scanSubnets"] = cfg.res.scanSubnets;

  JsonObject t = doc["time"].to<JsonObject>();
  t["tz"] = cfg.time.tz; t["ntp"] = cfg.time.ntp;

  JsonObject l = doc["log"].to<JsonObject>();
  l["changeDays"] = cfg.log.changeDays; l["relayDays"] = cfg.log.relayDays; l["queryDays"] = cfg.log.queryDays;
  l["queryPersist"] = cfg.log.queryPersist; l["maxKB"] = cfg.log.maxKB; l["flushMin"] = cfg.log.flushMin;

  JsonObject nt = doc["notify"].to<JsonObject>();
  nt["syslog"] = cfg.notify.syslog; nt["syslogPort"] = cfg.notify.syslogPort;
  JsonObject a = doc["auth"].to<JsonObject>();
  a["user"] = cfg.auth.user;
  if (withSecrets) a["pass"] = cfg.auth.pass;

  JsonArray pa = doc["printers"].to<JsonArray>();
  for (auto& p : cfg.printers) {
    JsonObject o = pa.add<JsonObject>();
    o["name"] = p.name; o["ip"] = p.ip; o["model"] = p.model; o["location"] = p.location; o["note"] = p.note;
  }
  JsonArray ha = doc["hosts"].to<JsonArray>();
  for (auto& h : cfg.hosts) {
    JsonObject o = ha.add<JsonObject>();
    o["name"] = h.name; o["aliases"] = h.aliases; o["fallback4"] = h.fallback4;
    o["fallback6"] = h.fallback6; o["note"] = h.note; o["enabled"] = h.enabled;
    o["relayPort"] = h.relayPort;
    o["mac"] = h.mac;
    o["scanName"] = h.scanName;
    if (h.share.length()) o["share"] = h.share;
    if (h.docPath.length()) o["docPath"] = h.docPath;
    if (h.login.length()) o["login"] = h.login;
  }
}

void configFromJson(JsonVariantConst d) {
  JsonVariantConst n = d["net"];
  cfg.net.dhcp     = jbool(n["dhcp"], cfg.net.dhcp);
  cfg.net.autoFallback = jbool(n["autoFallback"], cfg.net.autoFallback);
  cfg.net.ip       = jstr(n["ip"], cfg.net.ip);
  cfg.net.mask     = jstr(n["mask"], cfg.net.mask);
  cfg.net.gw       = jstr(n["gw"], cfg.net.gw);
  cfg.net.dns      = jstr(n["dns"], cfg.net.dns);
  cfg.net.hostname = jstr(n["hostname"], cfg.net.hostname);
  if (!cfg.net.hostname.length()) cfg.net.hostname = "xerox-dns";

  JsonVariantConst s = d["dns"];
  cfg.dns.upstream1  = jstr(s["upstream1"], cfg.dns.upstream1);
  cfg.dns.upstream2  = jstr(s["upstream2"], cfg.dns.upstream2);
  cfg.dns.ttl        = constrain(juint(s["ttl"], cfg.dns.ttl), 1u, 86400u);
  cfg.dns.domain     = jstr(s["domain"], cfg.dns.domain);
  cfg.dns.anySuffix  = jbool(s["anySuffix"], cfg.dns.anySuffix);
  cfg.dns.answerAAAA = jbool(s["answerAAAA"], cfg.dns.answerAAAA);
  cfg.dns.restrict   = jbool(s["restrict"], cfg.dns.restrict);
  while (cfg.dns.domain.startsWith(".")) cfg.dns.domain.remove(0, 1);

  JsonVariantConst r = d["res"];
  cfg.res.interval   = constrain(juint(r["interval"], cfg.res.interval), 5u, 86400u);
  if (r.is<JsonObjectConst>() && !r["onDemand"].is<bool>())   // config from v1.1.0 or older: 30 s polling
    cfg.res.interval = max(cfg.res.interval, (uint32_t)1800);
  cfg.res.onDemand   = jbool(r["onDemand"], cfg.res.onDemand);
  cfg.res.timeoutMs  = constrain(juint(r["timeoutMs"], cfg.res.timeoutMs), 200u, 5000u);
  cfg.res.nbns       = jbool(r["nbns"], cfg.res.nbns);
  cfg.res.llmnr      = jbool(r["llmnr"], cfg.res.llmnr);
  cfg.res.mdns       = jbool(r["mdns"], cfg.res.mdns);
  cfg.res.passive    = jbool(r["passive"], cfg.res.passive);
  cfg.res.extraBcast = jstr(r["extraBcast"], cfg.res.extraBcast);
  cfg.res.dnsLookup  = jbool(r["dnsLookup"], cfg.res.dnsLookup);
  cfg.res.scanSubnets = jstr(r["scanSubnets"], cfg.res.scanSubnets);

  JsonVariantConst t = d["time"];
  cfg.time.tz  = jstr(t["tz"], cfg.time.tz);
  cfg.time.ntp = jstr(t["ntp"], cfg.time.ntp);

  JsonVariantConst l = d["log"];
  cfg.log.changeDays   = constrain(juint(l["changeDays"], cfg.log.changeDays), 1u, 365u);
  cfg.log.relayDays    = constrain(juint(l["relayDays"], cfg.log.relayDays), 1u, 365u);
  cfg.log.queryDays    = constrain(juint(l["queryDays"], cfg.log.queryDays), 1u, 30u);
  cfg.log.queryPersist = jbool(l["queryPersist"], cfg.log.queryPersist);
  cfg.log.maxKB        = constrain(juint(l["maxKB"], cfg.log.maxKB), 64u, 800u);   // re-capped at runtime to 60 % of the FS
  cfg.log.flushMin     = constrain(juint(l["flushMin"], cfg.log.flushMin), 1u, 60u);

  JsonVariantConst nt = d["notify"];
  cfg.notify.syslog = jstr(nt["syslog"], cfg.notify.syslog);
  cfg.notify.syslog.trim();
  cfg.notify.syslogPort = constrain(juint(nt["syslogPort"], cfg.notify.syslogPort), 1u, 65535u);

  JsonVariantConst a = d["auth"];
  cfg.auth.user = jstr(a["user"], cfg.auth.user);
  String p = jstr(a["pass"], "");
  if (p.length()) cfg.auth.pass = p;           // blank = keep current password

  if (d["printers"].is<JsonArrayConst>()) {
    std::vector<PrinterCfg> list;
    printersFromJson(d["printers"].as<JsonArrayConst>(), list);
    if (validatePrinters(list) == "") setPrinters(list);
  }
  if (d["hosts"].is<JsonArrayConst>()) {
    std::vector<HostCfg> list;
    hostsFromJson(d["hosts"].as<JsonArrayConst>(), list);
    if (validateHosts(list) == "") setHosts(list);
  }
}

void configLoad() {
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f) {
    Serial.println("[CFG] no config file, using defaults");
    return;
  }
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) {
    Serial.printf("[CFG] parse error: %s, using defaults\n", e.c_str());
    return;
  }
  configFromJson(doc.as<JsonVariantConst>());
  Serial.printf("[CFG] loaded: %u hosts, %u printers\n", (unsigned)cfg.hosts.size(), (unsigned)cfg.printers.size());
}

bool configSave() {
  JsonDocument doc;
  configToJson(doc, true);
  File f = LittleFS.open("/config.tmp", "w");
  if (!f) return false;
  size_t n = serializeJson(doc, f);
  f.close();
  if (!n) return false;
  LittleFS.remove(CONFIG_FILE);
  return LittleFS.rename("/config.tmp", CONFIG_FILE);
}

void factoryReset() {
  LittleFS.remove(CONFIG_FILE);
  LittleFS.remove(STATE_FILE);
}

// ---- Last known addresses (survive reboot) ---------------------------------
void stateMarkDirty() {
  stateDirty = true;
  stateDirtyMs = millis();
}

static void stateSave() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (size_t i = 0; i < cfg.hosts.size() && i < hostState.size(); i++) {
    const HostState& h = hostState[i];
    JsonObject o = arr.add<JsonObject>();
    o["n"]    = cfg.hosts[i].name;
    o["v4"]   = ip4Str(h.v4);
    o["seen"] = (uint32_t)h.lastSeen;
    o["ts"]   = h.lastSeenStr;
    o["m"]    = h.method;
    JsonArray v6 = o["v6"].to<JsonArray>();
    for (int k = 0; k < h.v6n; k++) v6.add(ip6Str(h.v6[k].a));
  }
  File f = LittleFS.open(STATE_FILE, "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

void stateLoop() {
  if (stateDirty && millis() - stateDirtyMs > 15000) {
    stateDirty = false;
    stateSave();
  }
}

void stateLoad() {
  hostState.assign(cfg.hosts.size(), HostState());
  setPrinters(cfg.printers);   // fills printerState with parsed IPs
  File f = LittleFS.open(STATE_FILE, "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f)) { f.close(); return; }
  f.close();
  for (JsonVariantConst o : doc.as<JsonArrayConst>()) {
    String n = jstr(o["n"], "");
    for (size_t i = 0; i < cfg.hosts.size(); i++) {
      if (!cfg.hosts[i].name.equalsIgnoreCase(n)) continue;
      HostState& h = hostState[i];
      parseIp4(jstr(o["v4"], ""), h.v4);
      h.lastSeen    = (time_t)juint(o["seen"], 0);
      h.lastSeenStr = jstr(o["ts"], "");
      h.method      = jstr(o["m"], "");
      if (h.method.length()) h.method += " (ก่อนรีบูต)";
      for (JsonVariantConst a : o["v6"].as<JsonArrayConst>()) {
        if (h.v6n >= MAX_V6) break;
        if (parseIp6(jstr(a, ""), h.v6[h.v6n].a)) { h.v6[h.v6n].seenMs = 0; h.v6n++; }
      }
      break;
    }
  }
}
