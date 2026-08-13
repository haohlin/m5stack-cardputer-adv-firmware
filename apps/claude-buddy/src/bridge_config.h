#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

struct BridgeStoredConfig {
  bool valid;
  char ssid[33];
  char pass[65];
  char host[64];
  uint16_t port;
  char token[97];
};

void bridgeConfigLoad();
const BridgeStoredConfig& bridgeConfig();
uint32_t bridgeConfigVersion();
bool bridgeConfigValid();
bool bridgeConfigSave(const BridgeStoredConfig& cfg);
bool bridgeConfigSaveJson(JsonDocument& doc, char* error, size_t errorLen);
bool bridgeConfigSaveFromFile(const char* path, char* error, size_t errorLen);
bool bridgeConfigSaveWifi(const char* ssid, const char* pass, char* error, size_t errorLen);
void bridgeConfigClear();
