#pragma once
#include <Arduino.h>

// Proactive alerts: one-way UDP Syslog (RFC 3164) to the server in cfg.notify. UDP needs no
// answer from the server, so it still gets out when another device uses the board's IPv4.
// severity: 2 = critical, 4 = warning, 5 = notice, 6 = info
bool notifySend(int severity, const String& text);    // false: no server configured / send failed
