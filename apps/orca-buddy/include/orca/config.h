#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orca {

struct WifiConfig {
  std::string ssid;
  std::string passphrase;

  bool operator==(const WifiConfig& other) const;
};

struct PairingConfig {
  std::string endpoint;
  std::string bearer;
  std::string ca;

  bool operator==(const PairingConfig& other) const;
};

struct DeviceConfig {
  bool hasWifi = false;
  bool hasPairing = false;
  WifiConfig wifi;
  PairingConfig pairing;

  bool operator==(const DeviceConfig& other) const;
};

struct WssEndpoint {
  std::string host;
  std::uint16_t port = 0;
  std::string path;
};

bool validateWifi(const WifiConfig& config);
bool validatePairing(const PairingConfig& config);
bool parseWssEndpoint(const std::string& value, WssEndpoint& output);

bool encodeConfigBlob(const DeviceConfig& config,
                      std::vector<std::uint8_t>& output);
bool decodeConfigBlob(const std::vector<std::uint8_t>& input,
                      DeviceConfig& output);

class ConfigStore {
 public:
  virtual ~ConfigStore() = default;
  virtual bool read(std::vector<std::uint8_t>& output) = 0;
  virtual bool write(const std::vector<std::uint8_t>& input) = 0;
  virtual bool erase() = 0;
};

class ConfigManager {
 public:
  explicit ConfigManager(ConfigStore& store);

  bool load();
  bool updateWifi(const WifiConfig& wifi);
  bool updatePairing(const PairingConfig& pairing);
  bool forget();
  const DeviceConfig& active() const;

 private:
  bool persist(const DeviceConfig& candidate);

  ConfigStore& store_;
  DeviceConfig active_;
};

}  // namespace orca
