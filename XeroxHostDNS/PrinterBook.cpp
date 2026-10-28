#include "PrinterBook.h"
#include "AppState.h"
#include "Util.h"
#include <LittleFS.h>

static const char*  BOOK_DIR    = "/book";
static const size_t MAX_ENTRIES = 600;

static String bookPath(const String& ip) { return String(BOOK_DIR) + "/" + ip + ".json"; }

static int printerIndex(const String& ip) {
  for (size_t i = 0; i < cfg.printers.size(); i++) if (cfg.printers[i].ip == ip) return i;
  return -1;
}

String bookSave(const String& ip, JsonArrayConst entries) {
  int idx = printerIndex(ip);
  if (idx < 0) return "ไม่พบเครื่องพิมพ์ " + ip;
  if (entries.size() > MAX_ENTRIES) return "รายการเกิน " + String(MAX_ENTRIES) + " รายการ";
  JsonDocument doc;
  doc["date"] = nowStr();
  JsonArray out = doc["entries"].to<JsonArray>();
  for (JsonVariantConst e : entries) {
    String server = e["server"] | "";
    server.trim();
    if (!server.length()) continue;
    JsonObject o = out.add<JsonObject>();
    o["nick"] = String(e["nick"] | "").substring(0, 80);
    o["server"] = server.substring(0, 120);
    o["port"] = e["port"] | 0;
    o["proto"] = String(e["proto"] | "").substring(0, 10);
    o["path"] = String(e["path"] | "").substring(0, 120);
  }
  if (!LittleFS.exists(BOOK_DIR)) LittleFS.mkdir(BOOK_DIR);
  File f = LittleFS.open(bookPath(ip), "w");
  if (!f) return "บันทึกไม่สำเร็จ";
  serializeJson(doc, f);
  f.close();
  if (idx < (int)printerState.size()) {
    printerState[idx].bookCount = out.size();
    printerState[idx].bookDate = doc["date"].as<String>();
  }
  return "";
}

void bookDelete(const String& ip) {
  LittleFS.remove(bookPath(ip));
  int idx = printerIndex(ip);
  if (idx >= 0 && idx < (int)printerState.size()) { printerState[idx].bookCount = 0; printerState[idx].bookDate = ""; }
}

void bookLoadMeta() {
  JsonDocument filter;
  filter["date"] = true;
  filter["entries"][0]["nick"] = true;
  for (size_t i = 0; i < cfg.printers.size() && i < printerState.size(); i++) {
    File f = LittleFS.open(bookPath(cfg.printers[i].ip), "r");
    if (!f) continue;
    JsonDocument doc;
    if (!deserializeJson(doc, f, DeserializationOption::Filter(filter))) {
      printerState[i].bookCount = doc["entries"].size();
      printerState[i].bookDate = doc["date"] | "";
    }
    f.close();
  }
}

void bookCleanup() {
  File dir = LittleFS.open(BOOK_DIR);
  if (!dir || !dir.isDirectory()) return;
  std::vector<String> remove;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String n = f.name();
    int slash = n.lastIndexOf('/');
    if (slash >= 0) n = n.substring(slash + 1);
    f.close();
    if (n.endsWith(".json") && printerIndex(n.substring(0, n.length() - 5)) < 0) remove.push_back(n);
  }
  for (auto& n : remove) LittleFS.remove(String(BOOK_DIR) + "/" + n);
}

bool bookAny() {
  for (auto& p : printerState) if (p.bookCount) return true;
  return false;
}

void bookJson(const String& ip, JsonDocument& doc) {
  File f = LittleFS.open(bookPath(ip), "r");
  if (!f) { doc["entries"].to<JsonArray>(); return; }
  deserializeJson(doc, f);
  f.close();
}

// "\\PC-ACC01\scan", "smb://pc-acc01.corp.local/", "PC-ACC01:445" -> "pc-acc01"
static String serverLabel(String s) {
  s.trim();
  s.toLowerCase();
  int p = s.indexOf("://");
  if (p >= 0) s = s.substring(p + 3);
  while (s.startsWith("\\") || s.startsWith("/")) s.remove(0, 1);
  int cut = s.length();
  for (const char* sep : {"\\", "/", ":"}) {
    int i = s.indexOf(sep);
    if (i >= 0 && i < cut) cut = i;
  }
  return s.substring(0, cut);
}

static String firstLabel(const String& s) {
  int d = s.indexOf('.');
  return d > 0 ? s.substring(0, d) : s;
}

String bookMatch(const String& name, const String& aliases, const std::vector<uint32_t>& ips, uint16_t relayPort) {
  std::vector<String> names;
  String all = name + "," + aliases;
  int start = 0;
  while (start < (int)all.length()) {
    int c = all.indexOf(',', start);
    if (c < 0) c = all.length();
    String n = all.substring(start, c);
    n.trim();
    n.toLowerCase();
    if (n.length()) names.push_back(n);
    start = c + 1;
  }

  JsonDocument filter;
  filter["entries"][0]["nick"] = true;
  filter["entries"][0]["server"] = true;
  filter["entries"][0]["port"] = true;
  String result;
  for (size_t i = 0; i < cfg.printers.size(); i++) {
    File f = LittleFS.open(bookPath(cfg.printers[i].ip), "r");
    if (!f) continue;
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, f, DeserializationOption::Filter(filter));
    f.close();
    if (e) continue;
    for (JsonVariantConst en : doc["entries"].as<JsonArrayConst>()) {
      String server = serverLabel(en["server"] | "");
      uint16_t port = en["port"] | 0;
      bool hit = false;
      uint32_t ip;
      if (parseIp4(server, ip)) {
        if (ip == g_myIp) hit = relayPort && port == relayPort;      // relay: ESP32 IP + this host's port
        else for (uint32_t x : ips) if (x && x == ip) hit = true;
      } else {
        for (auto& n : names)
          if (server == n || firstLabel(server) == firstLabel(n)) hit = true;
      }
      if (!hit) continue;
      String nick = en["nick"] | "";
      if (!nick.length()) nick = en["server"].as<String>();
      String item = nick + " (" + cfg.printers[i].name + ")";
      if (result.indexOf(item) < 0) result += (result.length() ? ", " : "") + item;
    }
  }
  return result;
}
