#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Persistent logs on LittleFS.
//  - one file per type per day: /logs/c20261027.tsv (c = IP change, r = relay, q = DNS query)
//  - lines are buffered in RAM and appended in one write every cfg.log.flushMin minutes
//  - files older than the retention days are deleted; when the total exceeds the
//    quota the oldest days are deleted first (DNS queries before anything else)
enum LogType : char { LOG_CHANGE = 'c', LOG_RELAY = 'r', LOG_QUERY = 'q' };

void   logStoreBegin();
void   logStoreLoop();
void   logStoreAdd(LogType type, const String& tsvFields);   // fields after the timestamp, tab separated
void   logStoreFlush();                                      // write the RAM buffer now
void   logStoreClear();                                      // delete every log file and the buffer
void   logStoreInfoJson(JsonDocument& doc);
bool   logStoreValidName(const String& name);                // "r20261027.tsv"
String logStorePath(const String& name);
String tsvClean(const String& s);                            // removes tabs / new lines
