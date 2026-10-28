#pragma once
#include <ArduinoJson.h>

// TCP relay: the printer connects to <ESP32-IP>:<relayPort> and the stream is
// forwarded to <current IPv4 of the host>:445. No file is stored on the ESP32.
void relayBegin();     // (re)create listeners from cfg.hosts - keeps running sessions
void relayStop();      // close listeners and sessions
void relayLoop();
bool relayBusy();      // at least one session is active
bool relayListening(uint16_t port);
void relayStatusJson(JsonArray out);
