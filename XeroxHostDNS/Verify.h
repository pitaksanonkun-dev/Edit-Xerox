#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// "Verify" button of the Add Host form: checks, from the ESP32 itself, that the
// PC can be found (MAC / name), that its SMB port answers and that every copier
// is reachable. Runs step by step inside loop() without blocking.
void verifyStart(const String& name, const String& aliases, const String& mac, const String& fallback4, uint16_t relayPort);
void verifyLoop();
bool verifyRunning();
void verifyJson(JsonDocument& doc);
