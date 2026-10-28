#pragma once
#include <ArduinoJson.h>
#include "AppState.h"

void   configLoad();
bool   configSave();
void   configToJson(JsonDocument& doc, bool withSecrets);
// Applies only the keys that are present in `d` (missing keys keep their current value).
void   configFromJson(JsonVariantConst d);

String validateHosts(const std::vector<HostCfg>& list);       // "" = ok, otherwise error message
String validatePrinters(const std::vector<PrinterCfg>& list);
void   hostsFromJson(JsonArrayConst a, std::vector<HostCfg>& out);
void   printersFromJson(JsonArrayConst a, std::vector<PrinterCfg>& out);
void   setHosts(const std::vector<HostCfg>& list);            // keeps runtime state of unchanged names
void   setPrinters(const std::vector<PrinterCfg>& list);

void   stateLoad();
void   stateMarkDirty();
void   stateLoop();                                            // debounced save of last known addresses
void   factoryReset();
