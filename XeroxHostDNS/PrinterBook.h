#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

// Scan destinations imported from each copier's address book (CSV exported in the
// Xerox Embedded Web Server, parsed in the browser). Stored as /book/<printer-ip>.json.
// Verify uses them to find which "Scan To" nicknames point at a PC.
String bookSave(const String& printerIp, JsonArrayConst entries);   // "" = ok, else error
void   bookDelete(const String& printerIp);
void   bookLoadMeta();                     // fills printerState[].bookCount / bookDate
void   bookCleanup();                      // removes books of printers that were deleted
bool   bookAny();
void   bookJson(const String& printerIp, JsonDocument& doc);
// Returns "nick (Printer), nick2 (Printer2)" for every entry whose server is one of
// the names / IPs, or the ESP32 IP with the relay port.
String bookMatch(const String& name, const String& aliases, const std::vector<uint32_t>& ips, uint16_t relayPort);
