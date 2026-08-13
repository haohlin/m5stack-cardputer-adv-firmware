#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

constexpr uint32_t BUDDY_BRIDGE_CONFIG_RECORD_MAGIC = 0x42434647U;
constexpr uint16_t BUDDY_BRIDGE_CONFIG_RECORD_VERSION = 1;

inline uint32_t buddyBridgeConfigChecksum(const void* data, size_t length) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  uint32_t hash = 2166136261U;
  for (size_t i = 0; i < length; ++i) {
    hash ^= bytes[i];
    hash *= 16777619U;
  }
  return hash;
}

template <typename Config>
struct BuddyBridgeConfigRecord {
  uint32_t magic;
  uint16_t formatVersion;
  uint16_t payloadSize;
  Config config;
  uint32_t checksum;
};

template <typename Config>
inline BuddyBridgeConfigRecord<Config> buddyBridgeConfigRecordMake(const Config& config) {
  using Record = BuddyBridgeConfigRecord<Config>;
  Record record = {};
  record.magic = BUDDY_BRIDGE_CONFIG_RECORD_MAGIC;
  record.formatVersion = BUDDY_BRIDGE_CONFIG_RECORD_VERSION;
  record.payloadSize = sizeof(Config);
  record.config = config;
  record.checksum = buddyBridgeConfigChecksum(&record, offsetof(Record, checksum));
  return record;
}

template <typename Config>
inline bool buddyBridgeConfigRecordExtract(const BuddyBridgeConfigRecord<Config>& record,
                                           Config* config) {
  using Record = BuddyBridgeConfigRecord<Config>;
  if (!config || record.magic != BUDDY_BRIDGE_CONFIG_RECORD_MAGIC ||
      record.formatVersion != BUDDY_BRIDGE_CONFIG_RECORD_VERSION ||
      record.payloadSize != sizeof(Config) ||
      record.checksum != buddyBridgeConfigChecksum(&record, offsetof(Record, checksum))) {
    return false;
  }
  *config = record.config;
  return true;
}
