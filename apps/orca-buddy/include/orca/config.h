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

class FallbackConfigStore final : public ConfigStore {
 public:
  FallbackConfigStore(ConfigStore& primary, ConfigStore& fallback);

  bool read(std::vector<std::uint8_t>& output) override;
  bool write(const std::vector<std::uint8_t>& input) override;
  bool erase() override;
  bool usingFallback() const;

 private:
  ConfigStore& primary_;
  ConfigStore& fallback_;
  bool usingFallback_ = false;
};

class ConfigManager {
 public:
  ConfigManager(ConfigStore& wifiStore, ConfigStore& pairingStore);

  bool load();
  bool updateWifi(const WifiConfig& wifi);
  bool updatePairing(const PairingConfig& pairing);
  bool forget();
  const DeviceConfig& active() const;

 private:
  bool persistWifi(const WifiConfig& wifi);
  bool persistPairing(const PairingConfig& pairing);

  ConfigStore& wifiStore_;
  ConfigStore& pairingStore_;
  DeviceConfig active_;
};

}  // namespace orca
