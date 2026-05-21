#pragma once
#include <Arduino.h>

// ESP32-S3 USB CDC writes can stall the main loop if the host has opened the
// port but is not draining it. Protocol echoes/acks to Serial are therefore
// best-effort so BLE traffic cannot starve keyboard scanning.
inline size_t safeSerialWrite(const char* data, size_t len) {
  if (!data || len == 0) return 0;
  int room = Serial.availableForWrite();
  if (room < (int)len) return 0;
  return Serial.write((const uint8_t*)data, len);
}

inline size_t safeSerialWrite(const uint8_t* data, size_t len) {
  return safeSerialWrite((const char*)data, len);
}
