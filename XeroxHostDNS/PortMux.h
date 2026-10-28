#pragma once
#include <Arduino.h>
#include <vector>

// Port multiplexer: lwIP in Arduino-ESP32 can only listen on 16 TCP ports, far fewer than
// one relay port per PC. Incoming TCP packets for any relay port are rewritten to one
// internal port (PORTMUX_PORT) before lwIP sees them, and the replies are rewritten back,
// so the copier still talks to <ESP32-IP>:<relay port> and the relay needs one listener.
#define PORTMUX_PORT 40445

bool     portMuxBegin();                                // hook the Ethernet interface (call when it has an IP; also feeds NetGuard)
void     portMuxSetPorts(const std::vector<uint16_t>& ports);
bool     portMuxHas(uint16_t port);                     // port is redirected
uint16_t portMuxOrigPort(uint32_t peerIp, uint16_t peerPort);   // port the peer connected to (0 = unknown)
