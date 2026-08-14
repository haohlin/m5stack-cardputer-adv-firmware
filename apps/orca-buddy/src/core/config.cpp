#include "orca/config.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace orca {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'C', 'B', '1'}};
constexpr std::uint8_t kSchemaVersion = 1;
constexpr std::size_t kHeaderBytes = 16;
constexpr std::size_t kMaximumCaBytes = 8192;

bool hasControl(const std::string& value, bool allowNewlines = false) {
  for (unsigned char c : value) {
    if (c == 0 || c == 127) return true;
    if (c < 32 && !(allowNewlines && (c == '\r' || c == '\n'))) return true;
  }
  return false;
}

bool validHost(const std::string& host) {
  if (host.empty() || host.size() > 253 || hasControl(host)) return false;
  if (host.front() == '[' && host.back() == ']') {
    if (host.size() < 4) return false;
    return std::all_of(host.begin() + 1, host.end() - 1, [](unsigned char c) {
      return std::isxdigit(c) != 0 || c == ':' || c == '.';
    });
  }
  if (host.front() == '.' || host.back() == '.') return false;
  for (unsigned char c : host) {
    if (!(std::isalnum(c) != 0 || c == '-' || c == '.')) return false;
  }
  return true;
}

bool parsePort(const std::string& text, std::uint32_t& output) {
  if (text.empty() || text.size() > 5) return false;
  std::uint32_t value = 0;
  for (unsigned char c : text) {
    if (std::isdigit(c) == 0) return false;
    value = value * 10u + static_cast<std::uint32_t>(c - '0');
  }
  if (value == 0 || value > 65535u) return false;
  output = value;
  return true;
}

bool validPem(const std::string& ca) {
  constexpr const char* kBegin = "-----BEGIN CERTIFICATE-----\n";
  constexpr const char* kEnd = "-----END CERTIFICATE-----";
  if (ca.size() < 256 || ca.size() > kMaximumCaBytes || hasControl(ca, true)) {
    return false;
  }
  if (ca.rfind(kBegin, 0) != 0) return false;
  const std::size_t end = ca.rfind(kEnd);
  if (end == std::string::npos || end <= std::char_traits<char>::length(kBegin)) {
    return false;
  }
  for (std::size_t i = end + std::char_traits<char>::length(kEnd); i < ca.size(); ++i) {
    if (ca[i] != '\r' && ca[i] != '\n') return false;
  }
  for (std::size_t i = std::char_traits<char>::length(kBegin); i < end; ++i) {
    const unsigned char c = static_cast<unsigned char>(ca[i]);
    if (!(std::isalnum(c) != 0 || c == '+' || c == '/' || c == '=' ||
          c == '\r' || c == '\n' || c == '-')) {
      return false;
    }
  }
  return true;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t length) {
  std::uint32_t crc = 0xffffffffu;
  for (std::size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1u) ^ ((crc & 1u) != 0u ? 0xedb88320u : 0u);
    }
  }
  return crc ^ 0xffffffffu;
}

void appendU16(std::vector<std::uint8_t>& output, std::uint16_t value) {
  output.push_back(static_cast<std::uint8_t>(value & 0xffu));
  output.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
}

void appendU32(std::vector<std::uint8_t>& output, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }
}

void appendString(std::vector<std::uint8_t>& output, const std::string& value) {
  appendU16(output, static_cast<std::uint16_t>(value.size()));
  output.insert(output.end(), value.begin(), value.end());
}

bool readU16(const std::vector<std::uint8_t>& input, std::size_t& offset,
             std::uint16_t& value) {
  if (offset + 2 > input.size()) return false;
  value = static_cast<std::uint16_t>(input[offset]) |
          static_cast<std::uint16_t>(input[offset + 1] << 8u);
  offset += 2;
  return true;
}

bool readU32(const std::vector<std::uint8_t>& input, std::size_t& offset,
             std::uint32_t& value) {
  if (offset + 4 > input.size()) return false;
  value = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    value |= static_cast<std::uint32_t>(input[offset++]) << shift;
  }
  return true;
}

bool readString(const std::vector<std::uint8_t>& input, std::size_t& offset,
                std::string& value) {
  std::uint16_t length = 0;
  if (!readU16(input, offset, length) || offset + length > input.size()) {
    return false;
  }
  value.assign(reinterpret_cast<const char*>(input.data() + offset), length);
  offset += length;
  return true;
}

}  // namespace

bool WifiConfig::operator==(const WifiConfig& other) const {
  return ssid == other.ssid && passphrase == other.passphrase;
}

bool PairingConfig::operator==(const PairingConfig& other) const {
  return endpoint == other.endpoint && bearer == other.bearer && ca == other.ca;
}

bool DeviceConfig::operator==(const DeviceConfig& other) const {
  return hasWifi == other.hasWifi && hasPairing == other.hasPairing &&
         wifi == other.wifi && pairing == other.pairing;
}

bool validateWifi(const WifiConfig& config) {
  if (config.ssid.empty() || config.ssid.size() > 32 || hasControl(config.ssid)) {
    return false;
  }
  if (config.passphrase.empty()) return true;
  return config.passphrase.size() >= 8 && config.passphrase.size() <= 63 &&
         !hasControl(config.passphrase);
}

bool parseWssEndpoint(const std::string& value, WssEndpoint& output) {
  constexpr const char* kPrefix = "wss://";
  if (value.rfind(kPrefix, 0) != 0 || value.size() > 320 ||
      value.find('?') != std::string::npos || value.find('#') != std::string::npos ||
      value.find('@') != std::string::npos || hasControl(value)) {
    return false;
  }
  const std::size_t authorityStart = std::char_traits<char>::length(kPrefix);
  const std::size_t pathStart = value.find('/', authorityStart);
  if (pathStart == std::string::npos || value.substr(pathStart) != "/device") {
    return false;
  }
  const std::string authority = value.substr(authorityStart, pathStart - authorityStart);
  if (authority.empty()) return false;

  std::string host;
  std::uint32_t port = 443;
  if (authority.front() == '[') {
    const std::size_t close = authority.find(']');
    if (close == std::string::npos) return false;
    host = authority.substr(0, close + 1);
    if (close + 1 < authority.size()) {
      if (authority[close + 1] != ':' || close + 2 == authority.size()) return false;
      const std::string portText = authority.substr(close + 2);
      if (!parsePort(portText, port)) return false;
    }
  } else {
    const std::size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
      host = authority.substr(0, colon);
      const std::string portText = authority.substr(colon + 1);
      if (!parsePort(portText, port)) return false;
    } else {
      host = authority;
    }
  }
  if (!validHost(host) || port == 0 || port > 65535) return false;
  output = {host, static_cast<std::uint16_t>(port), "/device"};
  return true;
}

bool validatePairing(const PairingConfig& config) {
  WssEndpoint endpoint;
  if (!parseWssEndpoint(config.endpoint, endpoint)) return false;
  if (config.bearer.size() < 32 || config.bearer.size() > 128) return false;
  if (!std::all_of(config.bearer.begin(), config.bearer.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_' || c == '-';
      })) return false;
  return validPem(config.ca);
}

bool encodeConfigBlob(const DeviceConfig& config,
                      std::vector<std::uint8_t>& output) {
  if ((config.hasWifi && !validateWifi(config.wifi)) ||
      (config.hasPairing && !validatePairing(config.pairing))) {
    return false;
  }
  if ((!config.hasWifi && (!config.wifi.ssid.empty() || !config.wifi.passphrase.empty())) ||
      (!config.hasPairing && (!config.pairing.endpoint.empty() ||
                             !config.pairing.bearer.empty() ||
                             !config.pairing.ca.empty()))) {
    return false;
  }

  std::vector<std::uint8_t> payload;
  appendString(payload, config.wifi.ssid);
  appendString(payload, config.wifi.passphrase);
  appendString(payload, config.pairing.endpoint);
  appendString(payload, config.pairing.bearer);
  appendString(payload, config.pairing.ca);
  if (payload.size() > std::numeric_limits<std::uint32_t>::max()) return false;

  output.clear();
  output.insert(output.end(), kMagic.begin(), kMagic.end());
  output.push_back(kSchemaVersion);
  output.push_back(static_cast<std::uint8_t>((config.hasWifi ? 1u : 0u) |
                                            (config.hasPairing ? 2u : 0u)));
  appendU16(output, 0);
  appendU32(output, static_cast<std::uint32_t>(payload.size()));
  appendU32(output, crc32(payload.data(), payload.size()));
  output.insert(output.end(), payload.begin(), payload.end());
  return true;
}

bool decodeConfigBlob(const std::vector<std::uint8_t>& input,
                      DeviceConfig& output) {
  if (input.size() < kHeaderBytes ||
      !std::equal(kMagic.begin(), kMagic.end(), input.begin()) ||
      input[4] != kSchemaVersion || (input[5] & ~3u) != 0u ||
      input[6] != 0 || input[7] != 0) {
    return false;
  }
  std::size_t headerOffset = 8;
  std::uint32_t payloadLength = 0;
  std::uint32_t checksum = 0;
  if (!readU32(input, headerOffset, payloadLength) ||
      !readU32(input, headerOffset, checksum) ||
      payloadLength != input.size() - kHeaderBytes ||
      checksum != crc32(input.data() + kHeaderBytes, payloadLength)) {
    return false;
  }

  DeviceConfig candidate;
  candidate.hasWifi = (input[5] & 1u) != 0u;
  candidate.hasPairing = (input[5] & 2u) != 0u;
  std::size_t offset = kHeaderBytes;
  if (!readString(input, offset, candidate.wifi.ssid) ||
      !readString(input, offset, candidate.wifi.passphrase) ||
      !readString(input, offset, candidate.pairing.endpoint) ||
      !readString(input, offset, candidate.pairing.bearer) ||
      !readString(input, offset, candidate.pairing.ca) ||
      offset != input.size()) {
    return false;
  }
  std::vector<std::uint8_t> canonical;
  if (!encodeConfigBlob(candidate, canonical) || canonical != input) return false;
  output = std::move(candidate);
  return true;
}

ConfigManager::ConfigManager(ConfigStore& store) : store_(store) {}

bool ConfigManager::load() {
  std::vector<std::uint8_t> blob;
  DeviceConfig candidate;
  if (!store_.read(blob) || !decodeConfigBlob(blob, candidate)) {
    active_ = DeviceConfig{};
    return false;
  }
  active_ = std::move(candidate);
  return true;
}

bool ConfigManager::persist(const DeviceConfig& candidate) {
  std::vector<std::uint8_t> blob;
  if (!encodeConfigBlob(candidate, blob) || !store_.write(blob)) return false;
  active_ = candidate;
  return true;
}

bool ConfigManager::updateWifi(const WifiConfig& wifi) {
  if (!validateWifi(wifi)) return false;
  DeviceConfig candidate = active_;
  candidate.hasWifi = true;
  candidate.wifi = wifi;
  return persist(candidate);
}

bool ConfigManager::updatePairing(const PairingConfig& pairing) {
  if (!validatePairing(pairing)) return false;
  DeviceConfig candidate = active_;
  candidate.hasPairing = true;
  candidate.pairing = pairing;
  return persist(candidate);
}

bool ConfigManager::forget() {
  if (!store_.erase()) return false;
  active_ = DeviceConfig{};
  return true;
}

const DeviceConfig& ConfigManager::active() const { return active_; }

}  // namespace orca
