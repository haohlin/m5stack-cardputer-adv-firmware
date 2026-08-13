#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class File;

class FakeLittleFS {
 public:
  uint32_t capacity = 65536;
  size_t nextWriteLimit = SIZE_MAX;
  std::map<std::string, std::vector<uint8_t>> files;
  std::set<std::string> directories;

  FakeLittleFS() { reset(); }

  void reset() {
    capacity = 65536;
    nextWriteLimit = SIZE_MAX;
    files.clear();
    directories = {"/"};
  }

  File open(const char* path, const char* mode = nullptr);
  bool mkdir(const char* path);
  bool remove(const char* path);
  bool rmdir(const char* path);
  bool rename(const char* from, const char* to);
  bool exists(const char* path) const;
  size_t totalBytes() const { return capacity; }
  size_t usedBytes() const;
  void seedFile(const std::string& path, const std::string& contents);
  std::string readText(const std::string& path) const;
};

inline FakeLittleFS LittleFS;

class File {
 public:
  File() = default;

  explicit operator bool() const { return open_; }
  bool isDirectory() const { return open_ && directory_; }
  size_t size() const {
    auto it = LittleFS.files.find(path_);
    return it == LittleFS.files.end() ? 0 : it->second.size();
  }
  const char* name() const { return name_.c_str(); }

  File openNextFile() {
    if (!open_ || !directory_ || next_ >= entries_.size()) return File();
    const auto entry = entries_[next_++];
    return fromPath(entry.first, entry.second);
  }

  size_t write(const uint8_t* data, size_t length) {
    if (!open_ || directory_ || !writable_) return 0;
    const size_t stored = std::min(length, LittleFS.nextWriteLimit);
    LittleFS.nextWriteLimit = SIZE_MAX;
    auto& contents = LittleFS.files[path_];
    contents.insert(contents.end(), data, data + stored);
    return stored;
  }

  void close() { open_ = false; }

 private:
  friend class FakeLittleFS;

  static std::string baseName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
  }

  static File fromPath(const std::string& path, bool directory, bool writable = false) {
    File file;
    file.open_ = true;
    file.directory_ = directory;
    file.writable_ = writable;
    file.path_ = path;
    file.name_ = baseName(path);
    if (directory) file.refreshEntries();
    return file;
  }

  void refreshEntries() {
    const std::string prefix = path_ == "/" ? "/" : path_ + "/";
    std::set<std::pair<std::string, bool>> unique;
    for (const auto& directory : LittleFS.directories) {
      if (directory == path_ || directory.rfind(prefix, 0) != 0) continue;
      const std::string rest = directory.substr(prefix.size());
      if (!rest.empty() && rest.find('/') == std::string::npos) {
        unique.insert({directory, true});
      }
    }
    for (const auto& file : LittleFS.files) {
      if (file.first.rfind(prefix, 0) != 0) continue;
      const std::string rest = file.first.substr(prefix.size());
      if (!rest.empty() && rest.find('/') == std::string::npos) {
        unique.insert({file.first, false});
      }
    }
    entries_.assign(unique.begin(), unique.end());
  }

  bool open_ = false;
  bool directory_ = false;
  bool writable_ = false;
  std::string path_;
  std::string name_;
  std::vector<std::pair<std::string, bool>> entries_;
  size_t next_ = 0;
};

inline File FakeLittleFS::open(const char* rawPath, const char* mode) {
  if (!rawPath) return File();
  const std::string path(rawPath);
  if (mode && std::strchr(mode, 'w')) {
    files[path].clear();
    return File::fromPath(path, false, true);
  }
  if (directories.count(path)) return File::fromPath(path, true);
  if (files.count(path)) return File::fromPath(path, false);
  return File();
}

inline bool FakeLittleFS::mkdir(const char* rawPath) {
  if (!rawPath) return false;
  directories.insert(rawPath);
  return true;
}

inline bool FakeLittleFS::remove(const char* rawPath) {
  return rawPath && files.erase(rawPath) != 0;
}

inline bool FakeLittleFS::rmdir(const char* rawPath) {
  if (!rawPath) return false;
  const std::string path(rawPath);
  const std::string prefix = path + "/";
  for (const auto& file : files) if (file.first.rfind(prefix, 0) == 0) return false;
  for (const auto& directory : directories) {
    if (directory != path && directory.rfind(prefix, 0) == 0) return false;
  }
  return directories.erase(path) != 0;
}

inline bool FakeLittleFS::rename(const char* rawFrom, const char* rawTo) {
  if (!rawFrom || !rawTo) return false;
  const std::string from(rawFrom), to(rawTo);
  if (exists(to.c_str()) || (!directories.count(from) && !files.count(from))) return false;
  const std::string prefix = from + "/";
  const std::string replacement = to + "/";
  std::vector<std::pair<std::string, std::vector<uint8_t>>> movedFiles;
  std::vector<std::string> movedDirectories;
  for (const auto& file : files) {
    if (file.first == from || file.first.rfind(prefix, 0) == 0) {
      movedFiles.push_back({file.first == from ? to : replacement + file.first.substr(prefix.size()), file.second});
    }
  }
  for (const auto& directory : directories) {
    if (directory == from || directory.rfind(prefix, 0) == 0) {
      movedDirectories.push_back(directory == from ? to : replacement + directory.substr(prefix.size()));
    }
  }
  for (const auto& file : movedFiles) files.erase(file.first == to ? from : prefix + file.first.substr(replacement.size()));
  for (const auto& directory : movedDirectories) directories.erase(directory == to ? from : prefix + directory.substr(replacement.size()));
  for (const auto& file : movedFiles) files[file.first] = file.second;
  for (const auto& directory : movedDirectories) directories.insert(directory);
  return true;
}

inline bool FakeLittleFS::exists(const char* rawPath) const {
  if (!rawPath) return false;
  return files.count(rawPath) || directories.count(rawPath);
}

inline size_t FakeLittleFS::usedBytes() const {
  size_t used = 0;
  for (const auto& file : files) used += file.second.size();
  return used;
}

inline void FakeLittleFS::seedFile(const std::string& path, const std::string& contents) {
  const size_t slash = path.find_last_of('/');
  if (slash != std::string::npos && slash != 0) directories.insert(path.substr(0, slash));
  files[path] = std::vector<uint8_t>(contents.begin(), contents.end());
}

inline std::string FakeLittleFS::readText(const std::string& path) const {
  auto it = files.find(path);
  if (it == files.end()) return "";
  return std::string(it->second.begin(), it->second.end());
}

class FakeJsonValue {
 public:
  FakeJsonValue() = default;
  explicit FakeJsonValue(std::string value) : string_(std::move(value)), hasString_(true) {}
  explicit FakeJsonValue(uint32_t value) : number_(value), hasNumber_(true) {}

  operator const char*() const { return hasString_ ? string_.c_str() : nullptr; }

  const char* operator|(const char* fallback) const {
    return hasString_ ? string_.c_str() : fallback;
  }

  template <typename T, typename = std::enable_if_t<std::is_integral<T>::value>>
  T operator|(T fallback) const {
    return hasNumber_ ? static_cast<T>(number_) : fallback;
  }

 private:
  std::string string_;
  uint32_t number_ = 0;
  bool hasString_ = false;
  bool hasNumber_ = false;
};

class JsonDocument {
 public:
  const FakeJsonValue& operator[](const char* key) const {
    auto it = values_.find(key);
    return it == values_.end() ? missing_ : it->second;
  }
  FakeJsonValue& operator[](const char* key) { return values_[key]; }
  void setString(const char* key, const char* value) { values_[key] = FakeJsonValue(value); }
  void setUInt(const char* key, uint32_t value) { values_[key] = FakeJsonValue(value); }

 private:
  std::map<std::string, FakeJsonValue> values_;
  FakeJsonValue missing_;
};

inline unsigned long fakeMillis = 0;
inline unsigned long millis() { return fakeMillis; }

inline int mbedtls_base64_decode(uint8_t* output, size_t capacity, size_t* outputLength,
                                 const uint8_t* input, size_t inputLength) {
  static const std::string alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint32_t accumulator = 0;
  int bits = -8;
  size_t written = 0;
  for (size_t i = 0; i < inputLength; ++i) {
    const char c = static_cast<char>(input[i]);
    if (c == '=') break;
    const size_t position = alphabet.find(c);
    if (position == std::string::npos) return -1;
    accumulator = (accumulator << 6) | static_cast<uint32_t>(position);
    bits += 6;
    if (bits >= 0) {
      if (written >= capacity) return -1;
      output[written++] = static_cast<uint8_t>((accumulator >> bits) & 0xff);
      bits -= 8;
    }
  }
  *outputLength = written;
  return 0;
}

inline std::string fakeSerialOutput;
inline size_t safeSerialWrite(const char* data, size_t length) {
  fakeSerialOutput.append(data, length);
  return length;
}
inline size_t bleWrite(const uint8_t*, size_t length) { return length; }
inline bool bleSecure() { return true; }
inline void bleClearBonds() {}

struct FakeSettings { bool wifi = true; };
inline FakeSettings fakeSettings;
inline FakeSettings& settings() { return fakeSettings; }
inline void settingsSave() {}

struct FakeStats {
  uint16_t approvals = 0;
  uint16_t denials = 0;
  uint32_t napSeconds = 0;
  uint8_t level = 0;
};
inline FakeStats fakeStats;
inline FakeStats& stats() { return fakeStats; }
inline uint16_t statsMedianVelocity() { return 0; }
inline void speciesIdxSave(uint8_t) {}

inline float halBatteryVolts() { return 3.8f; }
inline float halBatteryMilliAmps() { return 0.0f; }
inline float halVbusVolts() { return 5.0f; }

struct FakeEsp {
  uint32_t getFreeHeap() const { return 65536; }
};
inline FakeEsp ESP;

inline bool buddyMode = false;
inline bool gifAvailable = false;
inline void buddySetSpeciesIdx(uint8_t) {}
inline void characterClose() {}
inline bool characterInit(const char*) { return true; }
inline void petNameSet(const char*) {}
inline const char* petName() { return "pet"; }
inline void ownerSet(const char*) {}
inline const char* ownerName() { return "owner"; }

inline bool fakeBridgeConfigSaved = false;
inline bool bridgeConfigSaveFromFile(const char* path, char* error, size_t errorLength) {
  const std::string contents = LittleFS.readText(path ? path : "");
  const bool valid = !contents.empty() && contents.front() == '{' && contents.back() == '}' &&
                     contents.find("\"endpoint\":\"wss://") != std::string::npos &&
                     contents.find("\"token\":\"012345678901234567890123\"") != std::string::npos &&
                     contents.find("-----BEGIN CERTIFICATE-----") != std::string::npos &&
                     contents.find("-----END CERTIFICATE-----") != std::string::npos;
  fakeBridgeConfigSaved = valid;
  if (!valid && error && errorLength) std::snprintf(error, errorLength, "%s", "bad json");
  return valid;
}
inline bool bridgeConfigSaveJson(JsonDocument&, char*, size_t) { return true; }
inline void bridgeConfigClear() {}
