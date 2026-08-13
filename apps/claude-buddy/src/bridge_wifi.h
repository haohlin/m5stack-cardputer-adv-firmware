#pragma once

#include <Arduino.h>

void bridgeWifiInit(const char* deviceName);
void bridgeWifiPoll(bool enabled, bool bleLinked, const char* pageName, uint8_t batteryPct);
bool bridgeWifiConfigured();
bool bridgeWifiConnected();
const char* bridgeWifiStatus();
const char* bridgeWifiLastMessage();
const char* bridgeWifiDiagPhase();
const char* bridgeWifiDiagBootPhase();
const char* bridgeWifiDiagReset();
const char* bridgeWifiDiagEvent();
const char* bridgeWifiDiagIp();
uint8_t bridgeWifiDiagDisconnectReason();
uint32_t bridgeWifiDiagBootCount();
