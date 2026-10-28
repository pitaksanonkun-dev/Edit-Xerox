#include "WebUI.h"
#include "WebPage.h"
#include "AppState.h"
#include "ConfigStore.h"
#include "Discovery.h"
#include "HostDns.h"
#include "LogStore.h"
#include "NetGuard.h"
#include "PrinterMon.h"
#include "Relay.h"
#include "Resolver.h"
#include "Util.h"
#include "Verify.h"
#include <ETH.h>
#include <LittleFS.h>
#include <WebServer.h>

static WebServer server(80);
static uint32_t  rebootAt = 0;

// ---- Login ----------------------------------------------------------------
// The browser logs in on /api/login and gets a session cookie (kept in RAM: a reboot logs
// everyone out). Basic auth is still accepted for tools such as ESP32Finder.
static const uint32_t SESSION_IDLE_MS = 8UL * 3600 * 1000;
static const int      LOGIN_MAX_FAILS = 5;          // then logins are refused for LOGIN_LOCK_MS
static const uint32_t LOGIN_LOCK_MS   = 60000;

struct WebSession { String token; uint32_t last = 0; };
static WebSession webSess[4];
static int        loginFails = 0;
static uint32_t   loginLockAt = 0;

static String cookieToken() {
  String c = server.header("Cookie");
  int i = c.indexOf("xsid=");
  if (i < 0) return "";
  int e = c.indexOf(';', i);
  return c.substring(i + 5, e < 0 ? c.length() : e);
}

static WebSession* currentSession() {
  String t = cookieToken();
  if (t.length() != 32) return nullptr;
  for (auto& s : webSess)
    if (s.token == t && millis() - s.last < SESSION_IDLE_MS) return &s;
  return nullptr;
}

static bool loginLocked() {
  if (loginFails < LOGIN_MAX_FAILS) return false;
  if (millis() - loginLockAt < LOGIN_LOCK_MS) return true;
  loginFails = 0;
  return false;
}

static void loginFailed() {
  if (++loginFails >= LOGIN_MAX_FAILS) loginLockAt = millis();
}

static bool auth() {
  if (!cfg.auth.user.length()) return true;
  if (WebSession* s = currentSession()) { s->last = millis(); return true; }
  // Browsers keep re-sending Basic credentials from older firmware, which would make logout
  // impossible: Basic auth is only for tools (ESP32Finder sends no browser User-Agent).
  bool browser = server.header("User-Agent").indexOf("Mozilla") >= 0;
  if (!browser && server.hasHeader("Authorization") && !loginLocked()) {
    if (server.authenticate(cfg.auth.user.c_str(), cfg.auth.pass.c_str())) return true;
    loginFailed();
  }
  if (server.uri() == "/") server.send_P(200, "text/html; charset=utf-8", LOGIN_HTML);
  else server.send(401, "application/json", "{\"ok\":false,\"login\":true,\"error\":\"กรุณาเข้าสู่ระบบ\"}");
  return false;
}

// Streams the JSON in 1 KB pieces: with 100 hosts /api/status is too large to copy into a String.
class ChunkOut : public Print {
 public:
  size_t write(uint8_t c) override { buf[n++] = c; if (n == sizeof(buf)) flush(); return 1; }
  size_t write(const uint8_t* d, size_t len) override { for (size_t i = 0; i < len; i++) write(d[i]); return len; }
  void flush() override { if (n) server.sendContent((const char*)buf, n); n = 0; }
 private:
  uint8_t buf[1024];
  size_t n = 0;
};

static void sendJson(JsonDocument& doc, int code = 200) {
  server.setContentLength(measureJson(doc));
  server.send(code, "application/json", "");
  ChunkOut out;
  serializeJson(doc, out);
  out.flush();
}

static void sendOk(bool reboot = false) {
  JsonDocument d;
  d["ok"] = true;
  d["reboot"] = reboot;
  sendJson(d);
}

static void sendErr(const String& msg, int code = 400) {
  JsonDocument d;
  d["ok"] = false;
  d["error"] = msg;
  sendJson(d, code);
}

static bool parseBody(JsonDocument& doc) {
  if (!server.hasArg("plain")) { sendErr("ไม่มีข้อมูล"); return false; }
  DeserializationError e = deserializeJson(doc, server.arg("plain"));
  if (e) { sendErr(String("JSON ไม่ถูกต้อง: ") + e.c_str()); return false; }
  return true;
}

// ---- POST /api/login /api/logout /api/password ------------------------------
static void setSessionCookie(const String& token) {
  server.sendHeader("Set-Cookie", "xsid=" + token + "; Path=/; HttpOnly; SameSite=Strict" + (token.length() ? "" : "; Max-Age=0"));
}

static void handleLogin() {
  JsonDocument doc;
  if (!parseBody(doc)) return;
  if (loginLocked()) {
    sendErr("ใส่รหัสผิดหลายครั้ง – รอ " + String((LOGIN_LOCK_MS - (millis() - loginLockAt)) / 1000 + 1) + " วินาทีแล้วลองใหม่", 429);
    return;
  }
  if (cfg.auth.user.length() && !(cfg.auth.user == (doc["user"] | "") && cfg.auth.pass == (doc["pass"] | ""))) {
    loginFailed();
    sendErr("ชื่อผู้ใช้หรือรหัสผ่านไม่ถูกต้อง", 401);
    return;
  }
  loginFails = 0;
  WebSession* slot = &webSess[0];                        // a free slot, else the least recently used
  for (auto& s : webSess) {
    if (!s.token.length()) { slot = &s; break; }
    if (millis() - s.last > millis() - slot->last) slot = &s;
  }
  char tok[33];
  for (int i = 0; i < 4; i++) snprintf(tok + i * 8, 9, "%08lx", (unsigned long)esp_random());
  slot->token = tok;
  slot->last = millis();
  setSessionCookie(slot->token);
  sendOk();
}

static void handleLogout() {
  if (WebSession* s = currentSession()) *s = WebSession();
  setSessionCookie("");
  sendOk();
}

static void handlePassword() {
  if (!auth()) return;
  JsonDocument doc;
  if (!parseBody(doc)) return;
  String user = doc["user"] | "", oldPass = doc["old"] | "", pass = doc["pass"] | "";
  user.trim();
  if (oldPass != cfg.auth.pass) { sendErr("รหัสผ่านปัจจุบันไม่ถูกต้อง"); return; }
  if (!user.length() || user.length() > 32) { sendErr("ชื่อผู้ใช้ต้องมี 1–32 ตัวอักษร"); return; }
  if (pass.length() < 6 || pass.length() > 64) { sendErr("รหัสผ่านใหม่ต้องมี 6–64 ตัวอักษร"); return; }
  cfg.auth.user = user;
  cfg.auth.pass = pass;
  if (!configSave()) { sendErr("บันทึกลงหน่วยความจำไม่สำเร็จ", 500); return; }
  WebSession* me = currentSession();                     // other browsers must log in again
  for (auto& s : webSess) if (&s != me) s = WebSession();
  sendOk();
}

// ---- GET /api/status -------------------------------------------------------
static void handleStatus() {
  if (!auth()) return;
  JsonDocument d;
  JsonObject dev = d["device"].to<JsonObject>();
  dev["fw"] = FW_VERSION;
  dev["hostname"] = cfg.net.hostname;
  dev["mac"] = ETH.macAddress();
  dev["link"] = ETH.linkUp();
  dev["speed"] = ETH.linkUp() ? ETH.linkSpeed() : 0;
  dev["duplex"] = ETH.linkUp() && ETH.fullDuplex() ? "Full" : "Half";
  dev["ip"] = ip4Str(g_myIp);
  dev["mask"] = ip4Str(g_myMask);
  dev["gw"] = ip4Str((uint32_t)ETH.gatewayIP());
  dev["dns"] = ip4Str((uint32_t)ETH.dnsIP());
  dev["dhcp"] = cfg.net.dhcp;
  dev["netFallback"] = netGuardFallback();
  dev["netReason"] = netGuardReason();
  dev["uptime"] = uptimeStr();
  dev["time"] = timeValid() ? nowStr() : String("");
  dev["heap"] = ESP.getFreeHeap();
  dev["interval"] = cfg.res.interval;
  dev["onDemand"] = cfg.res.onDemand;
  dev["user"] = cfg.auth.user;
  dev["maxRelay"] = MAX_RELAY_PORTS;

  JsonObject st = d["stats"].to<JsonObject>();
  st["queries"] = dnsStats.queries;
  st["local"] = dnsStats.local;
  st["forwarded"] = dnsStats.forwarded;
  st["refused"] = dnsStats.refused;
  st["failed"] = dnsStats.failed;

  JsonArray ha = d["hosts"].to<JsonArray>();
  uint32_t now = millis();
  for (size_t i = 0; i < cfg.hosts.size() && i < hostState.size(); i++) {
    const HostCfg& c = cfg.hosts[i];
    const HostState& h = hostState[i];
    JsonObject o = ha.add<JsonObject>();
    o["name"] = c.name; o["aliases"] = c.aliases; o["fallback4"] = c.fallback4;
    o["fallback6"] = c.fallback6; o["note"] = c.note; o["enabled"] = c.enabled;
    o["ipv4"] = ip4Str(h.v4);
    JsonArray v6 = o["ipv6"].to<JsonArray>();
    for (int k = 0; k < h.v6n; k++) v6.add(ip6Str(h.v6[k].a));
    o["method"] = h.method;
    o["lastSeen"] = h.lastSeenStr;
    o["ago"] = h.lastSeenMs ? (int32_t)((now - h.lastSeenMs) / 1000) : -1;
    o["fails"] = h.fails;
    o["rounds"] = h.rounds;
    o["dnsHits"] = h.dnsHits;
    o["relayPort"] = c.relayPort;
    o["mac"] = c.mac;
    o["scanName"] = c.scanName;
    o["relayJobs"] = h.relayJobs;
    o["relayFails"] = h.relayFails;
    o["relayBytes"] = h.relayBytes;
    o["relayLast"] = h.relayLast;
    // a sighting between rounds (name announcement, ARP, a scan through the relay) counts at once
    const char* status = !c.enabled ? "disabled" : h.fails == 0 && (h.rounds || h.lastSeenMs) ? "online"
                       : h.rounds == 0 ? "pending" : "offline";
    o["status"] = status;
  }

  JsonArray pa = d["printers"].to<JsonArray>();
  for (size_t i = 0; i < cfg.printers.size() && i < printerState.size(); i++) {
    const PrinterCfg& c = cfg.printers[i];
    const PrinterState& p = printerState[i];
    JsonObject o = pa.add<JsonObject>();
    o["name"] = c.name; o["ip"] = c.ip; o["model"] = c.model; o["location"] = c.location; o["note"] = c.note;
    o["online"] = p.online; o["checked"] = p.checked;
    o["dnsQueries"] = p.dnsQueries; o["lastQuery"] = p.lastQuery; o["lastQueryTime"] = p.lastQueryTime;
  }

  JsonArray cl = d["changes"].to<JsonArray>();
  for (auto it = changeLog.rbegin(); it != changeLog.rend(); ++it) {
    JsonObject o = cl.add<JsonObject>();
    o["time"] = it->time; o["host"] = it->host; o["text"] = it->text;
  }
  relayStatusJson(d["sessions"].to<JsonArray>());
  JsonArray rl = d["relays"].to<JsonArray>();
  for (auto it = relayLog.rbegin(); it != relayLog.rend(); ++it) {
    JsonObject o = rl.add<JsonObject>();
    o["time"] = it->time; o["client"] = it->client; o["host"] = it->host; o["target"] = it->target;
    o["result"] = it->result; o["bytes"] = it->bytes; o["secs"] = it->secs; o["ok"] = it->ok;
  }
  JsonArray ql = d["queries"].to<JsonArray>();
  for (auto it = queryLog.rbegin(); it != queryLog.rend(); ++it) {
    JsonObject o = ql.add<JsonObject>();
    o["time"] = it->time; o["client"] = it->client; o["name"] = it->name;
    o["type"] = it->type; o["result"] = it->result;
  }
  sendJson(d);
}

// ---- Configuration endpoints -----------------------------------------------
static void handleGetConfig() {
  if (!auth()) return;
  JsonDocument d;
  configToJson(d, false);
  d.remove("hosts");
  d.remove("printers");
  sendJson(d);
}

static void handleSaveHosts() {
  if (!auth()) return;
  JsonDocument doc;
  if (!parseBody(doc)) return;
  if (!doc.is<JsonArray>()) { sendErr("ต้องเป็นรายการ (array)"); return; }
  std::vector<HostCfg> list;
  hostsFromJson(doc.as<JsonArrayConst>(), list);
  String err = validateHosts(list);
  if (err.length()) { sendErr(err); return; }
  resolverReset();
  dnsReset();
  setHosts(list);
  configSave();
  stateMarkDirty();
  relayBegin();
  sendOk();
}

static void handleSavePrinters() {
  if (!auth()) return;
  JsonDocument doc;
  if (!parseBody(doc)) return;
  if (!doc.is<JsonArray>()) { sendErr("ต้องเป็นรายการ (array)"); return; }
  std::vector<PrinterCfg> list;
  printersFromJson(doc.as<JsonArrayConst>(), list);
  String err = validatePrinters(list);
  if (err.length()) { sendErr(err); return; }
  printerMonReset();
  setPrinters(list);
  configSave();
  sendOk();
}

static void handleSaveSettings() {
  if (!auth()) return;
  JsonDocument doc;
  if (!parseBody(doc)) return;
  doc.remove("hosts");
  doc.remove("printers");
  doc.remove("auth");                      // changed only through /api/password
  JsonVariantConst n = doc["net"];
  if (!(n["dhcp"] | true)) {
    uint32_t a;
    if (!parseIp4(n["ip"] | "", a) || !parseIp4(n["mask"] | "", a) || !parseIp4(n["gw"] | "", a)) {
      sendErr("Static IP ต้องระบุ IP, Subnet mask และ Gateway ให้ถูกต้อง");
      return;
    }
  }
  String subnetErr = resolverCheckSubnets(doc["res"]["scanSubnets"] | "");
  if (subnetErr.length()) { sendErr(subnetErr); return; }
  NetCfg oldNet = cfg.net;
  ResolverCfg oldRes = cfg.res;
  TimeCfg oldTime = cfg.time;
  configFromJson(doc.as<JsonVariantConst>());
  if (!configSave()) { sendErr("บันทึกลงหน่วยความจำไม่สำเร็จ", 500); return; }
  bool reboot = oldNet.dhcp != cfg.net.dhcp || oldNet.ip != cfg.net.ip || oldNet.mask != cfg.net.mask ||
                oldNet.gw != cfg.net.gw || oldNet.dns != cfg.net.dns || oldNet.hostname != cfg.net.hostname ||
                oldTime.tz != cfg.time.tz || oldTime.ntp != cfg.time.ntp;
  if (oldRes.passive != cfg.res.passive || oldRes.mdns != cfg.res.mdns) resolverBegin();
  sendOk(reboot);
}

static void handleResolve() {
  if (!auth()) return;
  if (server.hasArg("i")) resolverPrioritize(server.arg("i").toInt());
  else resolverRefreshAll();
  sendOk();
}

static void handleBackup() {
  if (!auth()) return;
  JsonDocument d;
  configToJson(d, true);
  String out;
  serializeJsonPretty(d, out);
  server.sendHeader("Content-Disposition", "attachment; filename=\"xerox-host-dns-backup.json\"");
  server.send(200, "application/json", out);
}

static void handleRestore() {
  if (!auth()) return;
  JsonDocument doc;
  if (!parseBody(doc)) return;
  if (!doc["hosts"].is<JsonArray>() && !doc["net"].is<JsonObject>()) { sendErr("ไฟล์ backup ไม่ถูกต้อง"); return; }
  std::vector<HostCfg> hl;
  std::vector<PrinterCfg> pl;
  hostsFromJson(doc["hosts"].as<JsonArrayConst>(), hl);
  printersFromJson(doc["printers"].as<JsonArrayConst>(), pl);
  String err = validateHosts(hl);
  if (!err.length()) err = validatePrinters(pl);
  if (err.length()) { sendErr(err); return; }
  resolverReset();
  dnsReset();
  printerMonReset();
  configFromJson(doc.as<JsonVariantConst>());
  configSave();
  sendOk(true);
  rebootAt = millis() + 1500;
}

static void handleReboot() {
  if (!auth()) return;
  sendOk(true);
  rebootAt = millis() + 800;
}

static void handleFactory() {
  if (!auth()) return;
  factoryReset();
  logStoreClear();
  sendOk(true);
  rebootAt = millis() + 800;
}

static void handleClearLogs() {
  if (!auth()) return;
  changeLog.clear();
  queryLog.clear();
  relayLog.clear();
  if (server.arg("flash") == "1") logStoreClear();
  sendOk();
}

static void handleLogInfo() {
  if (!auth()) return;
  JsonDocument d;
  logStoreInfoJson(d);
  sendJson(d);
}

// Streams one daily log file (TSV) - the browser renders or downloads it.
static void handleLogFile() {
  if (!auth()) return;
  String name = server.arg("name");
  if (!logStoreValidName(name)) { sendErr("ชื่อไฟล์ไม่ถูกต้อง"); return; }
  logStoreFlush();
  File f = LittleFS.open(logStorePath(name), "r");
  if (!f) { sendErr("ไม่พบไฟล์", 404); return; }
  if (server.arg("dl") == "1") server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  server.streamFile(f, "text/tab-separated-values; charset=utf-8");
  f.close();
}

static void handleLogFlush() {
  if (!auth()) return;
  logStoreFlush();
  sendOk();
}

static void handleTest() {
  if (!auth()) return;
  String name = server.arg("name");
  name.trim();
  name.toLowerCase();
  JsonDocument d;
  int idx = dnsMatchHost(name);
  d["match"] = idx >= 0 ? cfg.hosts[idx].name : String("");
  if (idx >= 0) {
    d["ipv4"] = ip4Str(hostState[idx].v4);
    d["status"] = hostState[idx].fails == 0 ? "online" : "offline";
  }
  sendJson(d);
}

static void handleNeighbors() {
  if (!auth()) return;
  JsonDocument d;
  discNeighborsJson(d);
  sendJson(d);
}

static void handleScan() {
  if (!auth()) return;
  discRequestScan();
  sendOk();
}

static void handleVerifyStart() {
  if (!auth()) return;
  JsonDocument doc;
  if (!parseBody(doc)) return;
  String name = doc["name"] | "", mac = doc["mac"] | "";
  name.trim();
  mac.trim();
  if (!name.length() && !mac.length()) { sendErr("กรุณาระบุชื่อเครื่องหรือ MAC Address ก่อน Verify"); return; }
  verifyStart(name, doc["aliases"] | "", mac, doc["fallback4"] | "", (uint16_t)(doc["relayPort"] | 0));
  sendOk();
}

static void handleVerifyStatus() {
  if (!auth()) return;
  JsonDocument d;
  verifyJson(d);
  sendJson(d);
}

// ---------------------------------------------------------------------------
void webBegin() {
  server.on("/", HTTP_GET, []() {
    if (!auth()) return;
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/api/login", HTTP_POST, handleLogin);
  server.on("/api/logout", HTTP_POST, handleLogout);
  server.on("/api/password", HTTP_POST, handlePassword);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/hosts", HTTP_POST, handleSaveHosts);
  server.on("/api/printers", HTTP_POST, handleSavePrinters);
  server.on("/api/settings", HTTP_POST, handleSaveSettings);
  server.on("/api/resolve", HTTP_POST, handleResolve);
  server.on("/api/backup", HTTP_GET, handleBackup);
  server.on("/api/restore", HTTP_POST, handleRestore);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/factory", HTTP_POST, handleFactory);
  server.on("/api/clearlogs", HTTP_POST, handleClearLogs);
  server.on("/api/logs/info", HTTP_GET, handleLogInfo);
  server.on("/api/logs/file", HTTP_GET, handleLogFile);
  server.on("/api/logs/flush", HTTP_POST, handleLogFlush);
  server.on("/api/test", HTTP_GET, handleTest);
  server.on("/api/neighbors", HTTP_GET, handleNeighbors);
  server.on("/api/scan", HTTP_POST, handleScan);
  server.on("/api/verify", HTTP_POST, handleVerifyStart);
  server.on("/api/verify", HTTP_GET, handleVerifyStatus);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  const char* hdrs[] = {"Cookie", "User-Agent"};
  server.collectHeaders(hdrs, 2);
  server.begin();
}

void webLoop() {
  server.handleClient();
  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) {
    logStoreFlush();                       // keep the buffered log lines
    ESP.restart();
  }
}
