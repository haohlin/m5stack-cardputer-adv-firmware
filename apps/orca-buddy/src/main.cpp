#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Cardputer.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <WebSocketsClient.h>
#include <WiFi.h>

#include "orca/config.h"
#include "orca/protocol.h"
#include "orca/reconnect.h"
#include "orca/ui_model.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kWifiConnectTimeoutMs = 15000;
constexpr std::size_t kMaximumSerialCommandBytes = 12288;
constexpr std::size_t kVisibleRows = 4;
constexpr std::uint8_t kHidLeftBracket = 0x2f;
constexpr const char* kFallbackConfigPath = "/orca-buddy-config.bin";
constexpr const char* kFallbackStagingPath = "/orca-buddy-config.next";
constexpr const char* kFallbackPreviousPath = "/orca-buddy-config.prev";

class PreferencesConfigStore final : public orca::ConfigStore {
 public:
  bool read(std::vector<std::uint8_t>& output) override {
    Preferences preferences;
    if (!preferences.begin("orca-buddy", true)) return false;
    const std::size_t length = preferences.getBytesLength("config");
    if (length == 0 || length > 12288) {
      preferences.end();
      return false;
    }
    output.resize(length);
    const std::size_t read = preferences.getBytes("config", output.data(), length);
    preferences.end();
    if (read != length) {
      output.clear();
      return false;
    }
    return true;
  }

  bool write(const std::vector<std::uint8_t>& input) override {
    if (input.empty() || input.size() > 12288) return false;
    Preferences preferences;
    if (!preferences.begin("orca-buddy", false)) return false;
    const std::size_t written =
        preferences.putBytes("config", input.data(), input.size());
    preferences.end();
    return written == input.size();
  }

  bool erase() override {
    Preferences preferences;
    if (!preferences.begin("orca-buddy", false)) return false;
    const bool removed = !preferences.isKey("config") || preferences.remove("config");
    preferences.end();
    return removed;
  }
};

class SpiffsConfigStore final : public orca::ConfigStore {
 public:
  bool read(std::vector<std::uint8_t>& output) override {
    if (!SPIFFS.begin(false)) return false;
    if (readPath(kFallbackConfigPath, output)) return true;
    return readPath(kFallbackPreviousPath, output);
  }

  bool write(const std::vector<std::uint8_t>& input) override {
    if (input.empty() || input.size() > kMaximumSerialCommandBytes || !SPIFFS.begin(false)) {
      return false;
    }
    SPIFFS.remove(kFallbackStagingPath);
    File staging = SPIFFS.open(kFallbackStagingPath, FILE_WRITE);
    if (!staging) return false;
    const std::size_t written = staging.write(input.data(), input.size());
    staging.flush();
    staging.close();
    if (written != input.size()) {
      SPIFFS.remove(kFallbackStagingPath);
      return false;
    }

    const bool hasCurrent = SPIFFS.exists(kFallbackConfigPath);
    if (hasCurrent) {
      SPIFFS.remove(kFallbackPreviousPath);
      if (!SPIFFS.rename(kFallbackConfigPath, kFallbackPreviousPath)) {
        SPIFFS.remove(kFallbackStagingPath);
        return false;
      }
    }
    if (!SPIFFS.rename(kFallbackStagingPath, kFallbackConfigPath)) {
      if (hasCurrent) SPIFFS.rename(kFallbackPreviousPath, kFallbackConfigPath);
      return false;
    }
    SPIFFS.remove(kFallbackPreviousPath);
    return true;
  }

  bool erase() override {
    if (!SPIFFS.begin(false)) return false;
    bool removed = true;
    for (const char* path : {kFallbackConfigPath, kFallbackStagingPath, kFallbackPreviousPath}) {
      if (SPIFFS.exists(path) && !SPIFFS.remove(path)) removed = false;
    }
    return removed;
  }

 private:
  static bool readPath(const char* path, std::vector<std::uint8_t>& output) {
    File file = SPIFFS.open(path, FILE_READ);
    if (!file) return false;
    const std::size_t length = file.size();
    if (length == 0 || length > kMaximumSerialCommandBytes) {
      file.close();
      return false;
    }
    output.resize(length);
    const std::size_t read = file.readBytes(reinterpret_cast<char*>(output.data()), length);
    file.close();
    if (read != length) {
      output.clear();
      return false;
    }
    return true;
  }
};

struct WifiChoice {
  std::string ssid;
  std::int32_t rssi = 0;
  bool open = false;
};

enum class UiMode {
  Console,
  WifiScan,
  WifiPassword,
  WifiConnecting,
  Prompt,
  ForgetConfirm,
};

enum class WifiAttempt { None, Stored, Candidate };

PreferencesConfigStore preferencesConfigStore;
SpiffsConfigStore spiffsConfigStore;
orca::FallbackConfigStore configStore(preferencesConfigStore, spiffsConfigStore);
orca::ConfigManager configManager(configStore);
orca::ConsoleModel consoleModel;
orca::ReconnectBackoff reconnectBackoff;
orca::StoredWifiRetryPolicy storedWifiRetry;
WebSocketsClient webSocket;

UiMode uiMode = UiMode::Console;
WifiAttempt wifiAttempt = WifiAttempt::None;
std::vector<WifiChoice> wifiChoices;
std::size_t selectedWifi = 0;
std::size_t rowOffset = 0;
orca::WifiConfig candidateWifi;
std::uint32_t wifiAttemptDeadline = 0;
std::uint32_t nextReconnectAt = 0;
bool wifiWasConnected = false;
bool webSocketStarted = false;
bool webSocketConnected = false;
bool displayDirty = true;
std::string statusText = "BOOT";
std::string detailText;
std::string authorizationHeader;
std::string serialLine;
bool serialOverflow = false;

bool hasWord(const Keyboard_Class::KeysState& keys, char first,
             char second = '\0') {
  for (char value : keys.word) {
    if (value == first || (second != '\0' && value == second)) return true;
  }
  return false;
}

bool hasHidKey(const Keyboard_Class::KeysState& keys, std::uint8_t key) {
  return std::find(keys.hid_keys.begin(), keys.hid_keys.end(), key) !=
         keys.hid_keys.end();
}

// Cardputer has no physical Escape key. Ctrl+[ is its standard Escape chord.
bool wifiCancelRequested(const Keyboard_Class::KeysState& keys) {
  return keys.del || (keys.ctrl && hasHidKey(keys, kHidLeftBracket));
}

std::string clipped(const std::string& value, std::size_t maximum) {
  if (value.size() <= maximum) return value;
  return value.substr(0, maximum);
}

void setStatus(std::string status, std::string detail = {}) {
  statusText = std::move(status);
  detailText = std::move(detail);
  displayDirty = true;
}

void stopWebSocket() {
  if (webSocketStarted) webSocket.disconnect();
  webSocketStarted = false;
  webSocketConnected = false;
  authorizationHeader.clear();
}

bool sendWebSocketText(const std::string& text) {
  return webSocketConnected && !text.empty() &&
         text.size() <= orca::kMaximumFrameBytes &&
         webSocket.sendTXT(reinterpret_cast<const std::uint8_t*>(text.data()),
                           text.size());
}

void applyServerFrame(const std::uint8_t* payload, std::size_t length) {
  if (length == 0 || length > orca::kMaximumFrameBytes) {
    setStatus("ERROR", "WSS frame rejected");
    stopWebSocket();
    return;
  }
  JsonDocument syntaxCheck;
  const DeserializationError jsonError =
      deserializeJson(syntaxCheck, payload, length);
  if (jsonError) {
    setStatus("ERROR", "Invalid server JSON");
    stopWebSocket();
    return;
  }
  const std::string frame(reinterpret_cast<const char*>(payload), length);
  orca::ServerEvent event;
  if (!orca::parseServerFrame(frame, event)) {
    setStatus("ERROR", "Server message rejected");
    stopWebSocket();
    return;
  }
  consoleModel.apply(event);
  if (event.type == orca::ServerEventType::Question) {
    setStatus("ATTENTION", "Answer requested");
  } else {
    displayDirty = true;
  }
}

void webSocketEvent(WStype_t type, std::uint8_t* payload, std::size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      webSocketConnected = true;
      setStatus("ONLINE", "Bridge connected");
      sendWebSocketText(orca::encodeHello());
      sendWebSocketText(orca::encodeState());
      break;
    case WStype_DISCONNECTED:
      webSocketConnected = false;
      if (WiFi.status() == WL_CONNECTED && configManager.active().hasPairing) {
        setStatus("RECONNECT", "Bridge unavailable");
      }
      break;
    case WStype_TEXT:
      applyServerFrame(payload, length);
      break;
    case WStype_BIN:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
      setStatus("ERROR", "Unsupported WSS frame");
      stopWebSocket();
      break;
    default:
      break;
  }
}

void startWebSocket() {
  if (webSocketStarted || WiFi.status() != WL_CONNECTED ||
      !configManager.active().hasPairing) {
    return;
  }
  orca::WssEndpoint endpoint;
  const orca::PairingConfig& pairing = configManager.active().pairing;
  if (!orca::parseWssEndpoint(pairing.endpoint, endpoint)) {
    setStatus("ERROR", "Pairing endpoint invalid");
    return;
  }
  authorizationHeader = "Authorization: Bearer " + pairing.bearer + "\r\n";
  webSocket.onEvent(webSocketEvent);
  webSocket.setExtraHeaders(authorizationHeader.c_str());
  webSocket.setReconnectInterval(5000);
  webSocket.enableHeartbeat(15000, 3000, 2);
  webSocket.beginSslWithCA(endpoint.host.c_str(), endpoint.port,
                           endpoint.path.c_str(), pairing.ca.c_str());
  webSocketStarted = true;
  setStatus("CONNECTING", "Opening secure bridge");
}

void beginWifiAttempt(const orca::WifiConfig& wifi, WifiAttempt attempt) {
  stopWebSocket();
  WiFi.disconnect(false, false);
  delay(40);
  WiFi.begin(wifi.ssid.c_str(),
             wifi.passphrase.empty() ? nullptr : wifi.passphrase.c_str());
  wifiAttempt = attempt;
  wifiAttemptDeadline = millis() + kWifiConnectTimeoutMs;
  uiMode = UiMode::WifiConnecting;
  setStatus("CONNECTING", attempt == WifiAttempt::Stored
                               ? "Connecting saved Wi-Fi"
                               : "Testing Wi-Fi");
}

void beginStoredWifi() {
  if (!configManager.active().hasWifi || storedWifiRetry.paused()) return;
  beginWifiAttempt(configManager.active().wifi, WifiAttempt::Stored);
}

void scheduleStoredReconnect() {
  if (!configManager.active().hasWifi) return;
  if (storedWifiRetry.paused()) {
    setStatus("OFFLINE", "Wi-Fi reconnect paused");
    return;
  }
  nextReconnectAt = millis() + reconnectBackoff.nextDelayMs();
  setStatus("RECONNECT", "Wi-Fi retry scheduled");
}

void cancelWifiAttempt() {
  const WifiAttempt cancelled = wifiAttempt;
  if (cancelled == WifiAttempt::None) return;

  wifiAttempt = WifiAttempt::None;
  WiFi.disconnect(false, false);
  wifiWasConnected = false;
  nextReconnectAt = 0;
  if (cancelled == WifiAttempt::Stored) {
    storedWifiRetry.pause();
    uiMode = UiMode::Console;
    setStatus("OFFLINE", "Wi-Fi reconnect paused");
  } else {
    uiMode = candidateWifi.passphrase.empty() ? UiMode::WifiScan
                                               : UiMode::WifiPassword;
    setStatus("CANCELLED", "Wi-Fi connection cancelled");
  }
}

void startWifiScan() {
  uiMode = UiMode::WifiScan;
  wifiChoices.clear();
  selectedWifi = 0;
  setStatus("SCAN", "Scanning Wi-Fi networks");
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 8);
  M5.Display.print("Scanning Wi-Fi...");
  const int count = WiFi.scanNetworks();
  if (count > 0) {
    for (int index = 0; index < count; ++index) {
      const std::string ssid = WiFi.SSID(index).c_str();
      if (ssid.empty() || ssid.size() > 32) continue;
      const auto duplicate = std::find_if(
          wifiChoices.begin(), wifiChoices.end(),
          [&](const WifiChoice& choice) { return choice.ssid == ssid; });
      if (duplicate != wifiChoices.end()) continue;
      wifiChoices.push_back(
          {ssid, WiFi.RSSI(index), WiFi.encryptionType(index) == WIFI_AUTH_OPEN});
      if (wifiChoices.size() == 16) break;
    }
  }
  WiFi.scanDelete();
  if (wifiChoices.empty()) setStatus("ERROR", "No networks; R rescans");
  else setStatus("SETUP", "Select Wi-Fi; Enter chooses");
}

void completeWifiAttempt() {
  const WifiAttempt completed = wifiAttempt;
  wifiAttempt = WifiAttempt::None;
  reconnectBackoff.reset();
  storedWifiRetry.resume();
  wifiWasConnected = true;
  if (completed == WifiAttempt::Candidate) {
    if (!configManager.updateWifi(candidateWifi)) {
      WiFi.disconnect(false, false);
      wifiWasConnected = false;
      uiMode = UiMode::Console;
      setStatus("ERROR", "Wi-Fi save failed; old config kept");
      scheduleStoredReconnect();
      return;
    }
    candidateWifi = {};
    setStatus("ONLINE", "Wi-Fi tested and saved");
  } else {
    setStatus("ONLINE", "Wi-Fi connected");
  }
  uiMode = UiMode::Console;
  startWebSocket();
}

void pollWifi() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (wifiAttempt != WifiAttempt::None) {
    if (connected) {
      completeWifiAttempt();
      return;
    }
    if (orca::ReconnectBackoff::deadlineReached(millis(), wifiAttemptDeadline)) {
      const WifiAttempt failed = wifiAttempt;
      wifiAttempt = WifiAttempt::None;
      WiFi.disconnect(false, false);
      wifiWasConnected = false;
      if (failed == WifiAttempt::Candidate) {
        uiMode = UiMode::WifiPassword;
        setStatus("ERROR", "Wi-Fi test failed; Enter retries");
      } else {
        uiMode = UiMode::Console;
        scheduleStoredReconnect();
      }
    }
    return;
  }

  if (connected) {
    if (!wifiWasConnected) {
      reconnectBackoff.reset();
      setStatus("ONLINE", "Wi-Fi connected");
      startWebSocket();
    }
  } else {
    if (wifiWasConnected) {
      stopWebSocket();
      scheduleStoredReconnect();
    }
    if (uiMode == UiMode::Console && storedWifiRetry.shouldAttempt(
                                     configManager.active().hasWifi, millis(),
                                     nextReconnectAt)) {
      beginStoredWifi();
    }
  }
  wifiWasConnected = connected;
}

void drawHeader() {
  M5.Display.fillRect(0, 0, M5.Display.width(), 20, TFT_NAVY);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_NAVY);
  M5.Display.setCursor(5, 6);
  M5.Display.print("ORCA STATUS");
  const std::string status = consoleModel.hasQuestion() ? "ATTENTION" : statusText;
  M5.Display.setCursor(150, 6);
  M5.Display.print(clipped(status, 13).c_str());
}

void drawConsole() {
  drawHeader();
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setCursor(5, 25);
  M5.Display.printf("Runtime %s  worktrees %u", consoleModel.connected() ? "ready" : "idle",
                    static_cast<unsigned>(consoleModel.worktreeCount()));
  int y = 40;
  const auto& rows = consoleModel.rows();
  for (std::size_t index = rowOffset;
       index < rows.size() && index < rowOffset + kVisibleRows; ++index) {
    const auto& row = rows[index];
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(5, y);
    M5.Display.printf("%-12s %-8s %s", clipped(row.displayName, 12).c_str(),
                      clipped(row.workspaceStatus, 8).c_str(),
                      clipped(row.agentState, 8).c_str());
    y += 13;
  }
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setCursor(5, 95);
  const std::string detail = !consoleModel.notice().empty()
                                 ? consoleModel.notice()
                                 : detailText;
  M5.Display.print(clipped(detail, 38).c_str());
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.setCursor(5, 120);
  M5.Display.print("P prompt  W Wi-Fi  F forget");
}

void drawQuestion() {
  drawHeader();
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setCursor(5, 27);
  M5.Display.print(clipped(consoleModel.questionText(), 38).c_str());
  int y = 48;
  for (std::size_t index = 0; index < consoleModel.questionLabels().size(); ++index) {
    M5.Display.setTextColor(index == consoleModel.selectedLabel() ? TFT_BLACK : TFT_WHITE,
                            index == consoleModel.selectedLabel() ? TFT_YELLOW : TFT_BLACK);
    M5.Display.setCursor(8, y);
    M5.Display.printf("%c %s", index == consoleModel.selectedLabel() ? '>' : ' ',
                      clipped(consoleModel.questionLabels()[index], 30).c_str());
    y += 13;
  }
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.setCursor(5, 120);
  M5.Display.print("Up/down choose  Enter answer");
}

void drawWifiScan() {
  drawHeader();
  M5.Display.setTextSize(1);
  int y = 25;
  const std::size_t begin = selectedWifi > 3 ? selectedWifi - 3 : 0;
  for (std::size_t index = begin;
       index < wifiChoices.size() && index < begin + 7; ++index) {
    const bool selected = index == selectedWifi;
    M5.Display.setTextColor(selected ? TFT_BLACK : TFT_WHITE,
                            selected ? TFT_CYAN : TFT_BLACK);
    M5.Display.setCursor(5, y);
    M5.Display.printf("%c %-22s %4ld %s", selected ? '>' : ' ',
                      clipped(wifiChoices[index].ssid, 22).c_str(),
                      static_cast<long>(wifiChoices[index].rssi),
                      wifiChoices[index].open ? "open" : "key");
    y += 13;
  }
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.setCursor(5, 120);
  M5.Display.print("Up/down  Enter  R rescan");
}

void drawPassword() {
  drawHeader();
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(5, 30);
  M5.Display.print(clipped(candidateWifi.ssid, 32).c_str());
  M5.Display.setCursor(5, 52);
  M5.Display.print("Passphrase:");
  M5.Display.setCursor(5, 70);
  M5.Display.print(std::string(std::min<std::size_t>(candidateWifi.passphrase.size(), 32), '*').c_str());
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setCursor(5, 93);
  M5.Display.print(clipped(detailText, 38).c_str());
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.setCursor(5, 120);
  M5.Display.print("Type key  Del  Enter test");
}

void drawWifiConnecting() {
  drawHeader();
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setCursor(5, 34);
  M5.Display.print(wifiAttempt == WifiAttempt::Stored ? "Saved Wi-Fi" : "Selected Wi-Fi");
  M5.Display.setCursor(5, 52);
  M5.Display.print("Connecting...");
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setCursor(5, 78);
  M5.Display.print(clipped(detailText, 38).c_str());
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.setCursor(5, 120);
  M5.Display.print("Del / Ctrl+[ cancel");
}

void drawPrompt() {
  drawHeader();
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(5, 30);
  M5.Display.print("Prompt draft:");
  M5.Display.setCursor(5, 48);
  M5.Display.print(clipped(consoleModel.prompt(), 100).c_str());
  M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  M5.Display.setCursor(5, 120);
  M5.Display.print("Enter send  backtick cancel");
}

void drawScreen() {
  if (!displayDirty) return;
  displayDirty = false;
  M5.Display.fillScreen(TFT_BLACK);
  if (uiMode == UiMode::WifiScan) drawWifiScan();
  else if (uiMode == UiMode::WifiPassword) drawPassword();
  else if (uiMode == UiMode::WifiConnecting) drawWifiConnecting();
  else if (uiMode == UiMode::Prompt) drawPrompt();
  else if (uiMode == UiMode::ForgetConfirm) {
    drawHeader();
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.setCursor(5, 38);
    M5.Display.print("Forget Wi-Fi and pairing?");
    M5.Display.setCursor(5, 60);
    M5.Display.print("Enter confirms; backtick cancels");
  } else if (consoleModel.hasQuestion()) drawQuestion();
  else drawConsole();
}

void performForget() {
  if (!configManager.forget()) {
    uiMode = UiMode::Console;
    setStatus("ERROR", "Forget failed; config remains active");
    return;
  }
  stopWebSocket();
  WiFi.disconnect(true, false);
  wifiAttempt = WifiAttempt::None;
  storedWifiRetry.resume();
  wifiWasConnected = false;
  candidateWifi = {};
  consoleModel = orca::ConsoleModel{};
  Serial.println("OK configuration forgotten");
  startWifiScan();
}

void handleSerialCommand(const std::string& command) {
  orca::PairingConfig pairing;
  if (orca::parseProvisioningCommand(command, pairing)) {
    if (!configManager.updatePairing(pairing)) {
      Serial.println("ERR pairing persist failed; active config unchanged");
      setStatus("ERROR", "Pairing save failed");
      return;
    }
    stopWebSocket();
    if (configStore.usingFallback()) {
      Serial.println("INFO pairing configuration saved in local fallback storage");
    }
    Serial.println("OK secure pairing saved");
    setStatus("PAIR", "Secure pairing saved");
    startWebSocket();
    return;
  }
  if (command == "forget") {
    performForget();
    return;
  }
  Serial.println("ERR expected one complete orca-pair payload or forget");
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\n') {
      if (serialOverflow) {
        Serial.println("ERR serial command too large");
      } else {
        if (!serialLine.empty() && serialLine.back() == '\r') serialLine.pop_back();
        if (!serialLine.empty()) handleSerialCommand(serialLine);
      }
      serialLine.clear();
      serialOverflow = false;
      continue;
    }
    if (serialOverflow) continue;
    if (serialLine.size() >= kMaximumSerialCommandBytes) {
      serialLine.clear();
      serialOverflow = true;
      continue;
    }
    serialLine.push_back(value);
  }
}

void chooseWifi() {
  if (wifiChoices.empty() || selectedWifi >= wifiChoices.size()) return;
  candidateWifi = {wifiChoices[selectedWifi].ssid, {}};
  if (wifiChoices[selectedWifi].open) {
    beginWifiAttempt(candidateWifi, WifiAttempt::Candidate);
  } else {
    uiMode = UiMode::WifiPassword;
    setStatus("SETUP", "Enter 8-63 character key");
  }
}

void handleConsoleKeys(const Keyboard_Class::KeysState& keys) {
  if (consoleModel.hasQuestion()) {
    if (hasWord(keys, ';', ',') || hasWord(keys, ':', '<')) {
      consoleModel.moveQuestionSelection(-1);
      displayDirty = true;
    } else if (hasWord(keys, '.', '/') || hasWord(keys, '>', '?')) {
      consoleModel.moveQuestionSelection(1);
      displayDirty = true;
    } else if (keys.enter) {
      const std::string message = orca::encodeQuestionAnswer(
          consoleModel.questionId(),
          consoleModel.questionLabels()[consoleModel.selectedLabel()]);
      if (sendWebSocketText(message)) {
        consoleModel.clearQuestion();
        setStatus("ONLINE", "Answer sent");
      } else {
        setStatus("ERROR", "Answer not sent");
      }
    }
    return;
  }
  if (hasWord(keys, 'p', 'P')) {
    if (webSocketConnected) {
      uiMode = UiMode::Prompt;
      consoleModel.clearPrompt();
      setStatus("PROMPT", "Type draft");
    } else {
      setStatus("ERROR", "Bridge not connected");
    }
  } else if (hasWord(keys, 'w', 'W')) {
    startWifiScan();
  } else if (hasWord(keys, 'f', 'F')) {
    uiMode = UiMode::ForgetConfirm;
    setStatus("ATTENTION", "Confirm forget");
  } else if (hasWord(keys, ';', ':') && rowOffset > 0) {
    --rowOffset;
    displayDirty = true;
  } else if (hasWord(keys, '.', '>') &&
             rowOffset + kVisibleRows < consoleModel.rows().size()) {
    ++rowOffset;
    displayDirty = true;
  }
}

void appendTypedWords(const Keyboard_Class::KeysState& keys,
                      std::string& destination, std::size_t maximum) {
  for (char value : keys.word) {
    const unsigned char c = static_cast<unsigned char>(value);
    if (c >= 32 && c != 127 && destination.size() < maximum) {
      destination.push_back(value);
    }
  }
}

void handleKeyboard() {
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
  const Keyboard_Class::KeysState keys = M5Cardputer.Keyboard.keysState();
  if (uiMode == UiMode::WifiConnecting) {
    if (wifiCancelRequested(keys)) cancelWifiAttempt();
    return;
  }
  if (uiMode == UiMode::WifiScan) {
    if ((hasWord(keys, ';', ':')) && selectedWifi > 0) --selectedWifi;
    else if (hasWord(keys, '.', '>') && selectedWifi + 1 < wifiChoices.size()) ++selectedWifi;
    else if (hasWord(keys, 'r', 'R')) startWifiScan();
    else if (keys.enter) chooseWifi();
    displayDirty = true;
    return;
  }
  if (uiMode == UiMode::WifiPassword) {
    if (keys.del && !candidateWifi.passphrase.empty()) candidateWifi.passphrase.pop_back();
    else if (keys.enter) {
      if (orca::validateWifi(candidateWifi)) {
        beginWifiAttempt(candidateWifi, WifiAttempt::Candidate);
      } else {
        setStatus("ERROR", "Key must be 8-63 characters");
      }
    } else {
      appendTypedWords(keys, candidateWifi.passphrase, 63);
    }
    displayDirty = true;
    return;
  }
  if (uiMode == UiMode::Prompt) {
    if (hasWord(keys, '`', '~')) {
      consoleModel.clearPrompt();
      uiMode = UiMode::Console;
      setStatus("ONLINE", "Prompt cancelled");
    } else if (keys.del) {
      consoleModel.backspacePrompt();
      displayDirty = true;
    } else if (keys.enter) {
      const std::string message = orca::encodePromptDraft(consoleModel.prompt());
      if (sendWebSocketText(message)) {
        consoleModel.clearPrompt();
        uiMode = UiMode::Console;
        setStatus("ONLINE", "Prompt sent");
      } else {
        setStatus("ERROR", "Prompt not sent");
      }
    } else {
      for (char value : keys.word) consoleModel.appendPrompt(value);
      displayDirty = true;
    }
    return;
  }
  if (uiMode == UiMode::ForgetConfirm) {
    if (keys.enter) performForget();
    else if (hasWord(keys, '`', '~')) {
      uiMode = UiMode::Console;
      setStatus(webSocketConnected ? "ONLINE" : "RECONNECT", "Forget cancelled");
    }
    return;
  }
  handleConsoleKeys(keys);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const std::uint32_t serialStart = millis();
  while (!Serial && millis() - serialStart < 1200) delay(10);

  auto m5Config = M5.config();
  m5Config.serial_baudrate = 115200;
  m5Config.fallback_board = m5::board_t::board_M5CardputerADV;
  M5Cardputer.begin(m5Config, true);
  if (M5.Display.height() > M5.Display.width()) M5.Display.setRotation(1);
  M5.Display.setBrightness(120);
  M5.Display.setTextWrap(false);

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  serialLine.reserve(1024);
  configManager.load();

  Serial.println("Orca Cardputer Buddy ready");
  if (configManager.active().hasWifi) {
    beginStoredWifi();
  } else {
    startWifiScan();
  }
  if (!configManager.active().hasPairing) {
    setStatus("PAIR", "USB serial needs orca-pair payload");
  }
}

void loop() {
  M5Cardputer.update();
  pollSerial();
  pollWifi();
  if (webSocketStarted) webSocket.loop();
  handleKeyboard();
  drawScreen();
  delay(10);
}
