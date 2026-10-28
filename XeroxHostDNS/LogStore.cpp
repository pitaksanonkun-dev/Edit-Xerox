#include "LogStore.h"
#include "AppState.h"
#include "Util.h"
#include <LittleFS.h>
#include <algorithm>
#include <vector>

static const char*    LOG_DIR         = "/logs";
static const size_t   MAX_PENDING     = 200;     // RAM buffer (≈ 20 KB worst case)
static const size_t   FLUSH_AT        = 40;      // flush early when this many lines wait
static const uint32_t MAINTENANCE_MS  = 3600000; // retention / quota check every hour

struct Pending {
  char     type;
  uint32_t ms;        // millis() when logged - turned into a date once NTP time is known
  time_t   epoch;     // 0 if the clock was not set yet
  String   line;
};
static std::vector<Pending> pending;
static uint32_t lastFlush = 0, lastMaint = 0;
static bool     maintPending = true;

// ---------------------------------------------------------------------------
String tsvClean(const String& s) {
  String o = s;
  o.replace('\t', ' ');
  o.replace('\r', ' ');
  o.replace('\n', ' ');
  return o;
}

bool logStoreValidName(const String& n) {
  if (n.length() != 13 || !n.endsWith(".tsv")) return false;
  if (n[0] != LOG_CHANGE && n[0] != LOG_RELAY && n[0] != LOG_QUERY) return false;
  for (int i = 1; i < 9; i++) if (!isdigit((unsigned char)n[i])) return false;
  return true;
}

String logStorePath(const String& name) { return String(LOG_DIR) + "/" + name; }

static uint32_t quotaBytes() {
  uint32_t cap = (uint32_t)(LittleFS.totalBytes() * 6 / 10);   // keep 40 % free for LittleFS
  uint32_t want = (uint32_t)cfg.log.maxKB * 1024;
  return want < cap ? want : cap;
}

static uint16_t retentionDays(char t) {
  return t == LOG_CHANGE ? cfg.log.changeDays : t == LOG_RELAY ? cfg.log.relayDays : cfg.log.queryDays;
}

static long dayNumber(int y, int m, int d) {          // days since 1970-01-01 (civil calendar)
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static long fileDay(const String& name) {             // "r20261027.tsv" -> day number
  return dayNumber(name.substring(1, 5).toInt(), name.substring(5, 7).toInt(), name.substring(7, 9).toInt());
}

static long today() {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  return dayNumber(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

struct FileInfo { String name; size_t size; long day; };

static std::vector<FileInfo> listFiles() {
  std::vector<FileInfo> out;
  File dir = LittleFS.open(LOG_DIR);
  if (!dir || !dir.isDirectory()) return out;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String n = f.name();
    int slash = n.lastIndexOf('/');
    if (slash >= 0) n = n.substring(slash + 1);
    if (logStoreValidName(n)) out.push_back({n, f.size(), fileDay(n)});
    f.close();
  }
  return out;
}

// Deletes files past their retention, then the oldest days until under the quota.
static void maintenance() {
  std::vector<FileInfo> files = listFiles();
  bool haveTime = timeValid();
  long t = haveTime ? today() : 0;
  size_t total = 0;
  for (size_t i = 0; i < files.size();) {
    if (haveTime && t - files[i].day >= retentionDays(files[i].name[0])) {
      LittleFS.remove(logStorePath(files[i].name));
      Serial.printf("[LOG] expired %s\n", files[i].name.c_str());
      files.erase(files.begin() + i);
    } else {
      total += files[i].size;
      i++;
    }
  }
  uint32_t quota = quotaBytes();
  while (total > quota && !files.empty()) {
    size_t victim = 0;                               // oldest DNS-query day first, then the oldest of any type
    for (size_t i = 1; i < files.size(); i++) {
      bool qi = files[i].name[0] == LOG_QUERY, qv = files[victim].name[0] == LOG_QUERY;
      if ((qi && !qv) || (qi == qv && files[i].day < files[victim].day)) victim = i;
    }
    LittleFS.remove(logStorePath(files[victim].name));
    Serial.printf("[LOG] quota: removed %s\n", files[victim].name.c_str());
    total -= files[victim].size;
    files.erase(files.begin() + victim);
  }
}

void logStoreFlush() {
  if (pending.empty() || !timeValid()) return;       // dates need NTP; keep buffering until then
  time_t now = time(nullptr);
  uint32_t nowMs = millis();
  // group consecutive lines that go to the same file into one append
  size_t i = 0;
  while (i < pending.size()) {
    Pending& p = pending[i];
    if (!p.epoch) p.epoch = now - (time_t)((nowMs - p.ms) / 1000);
    struct tm tmv;
    localtime_r(&p.epoch, &tmv);
    char name[16];
    snprintf(name, sizeof(name), "%c%04d%02d%02d.tsv", p.type, tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    File f = LittleFS.open(logStorePath(name), "a");
    if (!f) { Serial.printf("[LOG] cannot write %s\n", name); break; }
    String chunk;
    size_t j = i;
    for (; j < pending.size(); j++) {
      Pending& q = pending[j];
      if (!q.epoch) q.epoch = now - (time_t)((nowMs - q.ms) / 1000);
      struct tm tq;
      localtime_r(&q.epoch, &tq);
      if (q.type != p.type || tq.tm_mday != tmv.tm_mday || tq.tm_mon != tmv.tm_mon) break;
      chunk += String((uint32_t)q.epoch) + "\t" + q.line + "\n";
    }
    f.print(chunk);
    f.close();
    i = j;
  }
  pending.erase(pending.begin(), pending.begin() + i);
  lastFlush = millis();
  maintPending = true;
}

void logStoreAdd(LogType type, const String& fields) {
  if (type == LOG_QUERY && !cfg.log.queryPersist) return;
  if (pending.size() >= MAX_PENDING) pending.erase(pending.begin());   // drop the oldest if NTP never came
  pending.push_back({(char)type, millis(), timeValid() ? time(nullptr) : 0, fields});
}

void logStoreBegin() {
  if (!LittleFS.exists(LOG_DIR)) LittleFS.mkdir(LOG_DIR);
  lastFlush = lastMaint = millis();
}

void logStoreLoop() {
  uint32_t now = millis();
  if (!pending.empty() && (pending.size() >= FLUSH_AT || now - lastFlush >= cfg.log.flushMin * 60000UL))
    logStoreFlush();
  if (timeValid() && (maintPending ? now - lastMaint >= 60000 : now - lastMaint >= MAINTENANCE_MS)) {
    maintenance();
    lastMaint = now;
    maintPending = false;
  }
}

void logStoreClear() {
  pending.clear();
  for (auto& f : listFiles()) LittleFS.remove(logStorePath(f.name));
}

void logStoreInfoJson(JsonDocument& doc) {
  std::vector<FileInfo> files = listFiles();
  std::sort(files.begin(), files.end(), [](const FileInfo& a, const FileInfo& b) {
    return a.day != b.day ? a.day > b.day : a.name < b.name;
  });
  size_t total = 0;
  JsonArray arr = doc["files"].to<JsonArray>();
  for (auto& f : files) {
    total += f.size;
    JsonObject o = arr.add<JsonObject>();
    o["name"] = f.name;
    o["type"] = String(f.name[0]);
    o["day"] = f.name.substring(1, 5) + "-" + f.name.substring(5, 7) + "-" + f.name.substring(7, 9);
    o["size"] = f.size;
  }
  doc["fsTotal"] = LittleFS.totalBytes();
  doc["fsUsed"] = LittleFS.usedBytes();
  doc["logBytes"] = total;
  doc["quota"] = quotaBytes();
  doc["pending"] = pending.size();
  uint32_t since = millis() - lastFlush, period = cfg.log.flushMin * 60000UL;
  doc["nextFlush"] = pending.empty() || since >= period ? 0 : (period - since) / 1000;
  doc["timeValid"] = timeValid();
}
