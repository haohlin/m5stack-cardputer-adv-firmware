#include "orca/config.h"
#include "orca/protocol.h"
#include "orca/reconnect.h"
#include "orca/storage.h"
#include "orca/ui_model.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: "          \
                << #condition << '\n';                                         \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

std::string testCa() {
  return "-----BEGIN CERTIFICATE-----\n" + std::string(256, 'A') +
         "\n-----END CERTIFICATE-----\n";
}

orca::PairingConfig validPairing() {
  return {"wss://192.0.2.10:17654/device",
          "AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_-ABCDE", testCa()};
}

orca::WifiConfig validWifi() {
  return {"Lab Network", "correct horse battery"};
}

std::string quoted(const std::string& value) {
  std::string result = "\"";
  for (char c : value) {
    if (c == '\n') result += "\\n";
    else if (c == '\r') result += "\\r";
    else if (c == '\\') result += "\\\\";
    else if (c == '"') result += "\\\"";
    else result += c;
  }
  return result + '"';
}

std::string provisioning(const orca::PairingConfig& pairing) {
  return "orca-pair {\"version\":\"orca-cardputer/v1\",\"endpoint\":" +
         quoted(pairing.endpoint) + ",\"bearer\":" + quoted(pairing.bearer) +
         ",\"ca\":" + quoted(pairing.ca) + "}";
}

class MemoryStore final : public orca::ConfigStore {
 public:
  bool failWrite = false;
  bool failErase = false;
  std::vector<std::uint8_t> bytes;

  bool read(std::vector<std::uint8_t>& output) override {
    if (bytes.empty()) return false;
    output = bytes;
    return true;
  }

  bool write(const std::vector<std::uint8_t>& input) override {
    if (failWrite) return false;
    bytes = input;
    return true;
  }

  bool erase() override {
    if (failErase) return false;
    bytes.clear();
    return true;
  }
};

void testProvisioningValidation() {
  const auto pairing = validPairing();
  orca::PairingConfig parsed;
  CHECK(orca::parseProvisioningCommand(provisioning(pairing), parsed));
  CHECK(parsed == pairing);

  CHECK(!orca::parseProvisioningCommand(
      "orca-pair {\"version\":\"orca-cardputer/v1\",\"endpoint\":\"wss://host/device\"}", parsed));
  CHECK(!orca::parseProvisioningCommand(
      provisioning({"ws://192.0.2.10:17654/device", pairing.bearer, pairing.ca}), parsed));
  CHECK(!orca::parseProvisioningCommand(
      provisioning({"wss://192.0.2.10:17654/device?token=leak", pairing.bearer, pairing.ca}), parsed));
  CHECK(!orca::parseProvisioningCommand(
      provisioning({pairing.endpoint, "short", pairing.ca}), parsed));
  CHECK(!orca::parseProvisioningCommand(
      provisioning({pairing.endpoint, std::string(42, 'A') + ".", pairing.ca}), parsed));
  CHECK(!orca::parseProvisioningCommand(
      provisioning({pairing.endpoint, pairing.bearer, std::string(9000, 'A')}), parsed));
  CHECK(!orca::parseProvisioningCommand(provisioning(pairing) + " junk", parsed));
  CHECK(!orca::parseProvisioningCommand(provisioning(pairing) + "\norca-pair {}", parsed));
  CHECK(!orca::parseProvisioningCommand(
      "orca-pair {\"version\":\"legacy/v1\",\"endpoint\":" + quoted(pairing.endpoint) +
      ",\"bearer\":" + quoted(pairing.bearer) + ",\"ca\":" + quoted(pairing.ca) + "}", parsed));
}

void testEndpointContract() {
  orca::WssEndpoint endpoint;
  CHECK(orca::parseWssEndpoint("wss://bridge.local:17654/device", endpoint));
  CHECK(endpoint.host == "bridge.local");
  CHECK(endpoint.port == 17654);
  CHECK(endpoint.path == "/device");
  CHECK(orca::parseWssEndpoint("wss://bridge.local/device", endpoint));
  CHECK(endpoint.port == 443);
  CHECK(!orca::parseWssEndpoint("wss://user@bridge.local/device", endpoint));
  CHECK(!orca::parseWssEndpoint("wss://bridge.local/other", endpoint));
  CHECK(!orca::parseWssEndpoint("wss://bridge.local/device#fragment", endpoint));
  CHECK(!orca::parseWssEndpoint("wss://bridge.local:" + std::string(100, '9') +
                                    "/device",
                                endpoint));
}

void testBlobChecksumAndVersion() {
  const orca::DeviceConfig input{true, true, validWifi(), validPairing()};
  std::vector<std::uint8_t> blob;
  CHECK(orca::encodeConfigBlob(input, blob));
  CHECK(blob.size() > input.pairing.ca.size());

  orca::DeviceConfig decoded;
  CHECK(orca::decodeConfigBlob(blob, decoded));
  CHECK(decoded == input);

  auto corrupt = blob;
  corrupt.back() ^= 0x01;
  CHECK(!orca::decodeConfigBlob(corrupt, decoded));

  auto wrongVersion = blob;
  wrongVersion[4] = 2;
  CHECK(!orca::decodeConfigBlob(wrongVersion, decoded));

  auto truncated = blob;
  truncated.pop_back();
  CHECK(!orca::decodeConfigBlob(truncated, decoded));
}

void testFailedUpdatesPreserveActiveConfig() {
  MemoryStore store;
  orca::ConfigManager manager(store);
  CHECK(manager.updateWifi(validWifi()));
  CHECK(manager.updatePairing(validPairing()));
  const auto active = manager.active();
  const auto persisted = store.bytes;

  store.failWrite = true;
  CHECK(!manager.updateWifi({"Replacement", "replacement passphrase"}));
  CHECK(manager.active() == active);
  CHECK(store.bytes == persisted);
  CHECK(!manager.updatePairing({"wss://other.local/device",
                                std::string(43, 'Z'), testCa()}));
  CHECK(manager.active() == active);

  store.failWrite = false;
  store.failErase = true;
  CHECK(!manager.forget());
  CHECK(manager.active() == active);
  CHECK(store.bytes == persisted);

  store.failErase = false;
  CHECK(manager.forget());
  CHECK(!manager.active().hasWifi);
  CHECK(!manager.active().hasPairing);
  CHECK(store.bytes.empty());
}

void testLoadFailsClosed() {
  MemoryStore store;
  store.bytes = {0, 1, 2, 3};
  orca::ConfigManager manager(store);
  CHECK(!manager.load());
  CHECK(!manager.active().hasWifi);
  CHECK(!manager.active().hasPairing);
}

void testFallbackStorePersistsCombinedConfigWhenPrimaryRejectsLargeUpdate() {
  MemoryStore primary;
  MemoryStore fallback;
  orca::ConfigManager primaryManager(primary);
  CHECK(primaryManager.updateWifi(validWifi()));

  primary.failWrite = true;
  orca::FallbackConfigStore store(primary, fallback);
  orca::ConfigManager manager(store);
  CHECK(manager.load());
  CHECK(manager.active().hasWifi);
  CHECK(!manager.active().hasPairing);
  CHECK(manager.updatePairing(validPairing()));
  CHECK(!fallback.bytes.empty());

  orca::FallbackConfigStore reloadedStore(primary, fallback);
  orca::ConfigManager reloaded(reloadedStore);
  CHECK(reloaded.load());
  CHECK(reloaded.active().hasWifi);
  CHECK(reloaded.active().hasPairing);
  CHECK(reloaded.active().wifi == validWifi());
  CHECK(reloaded.active().pairing == validPairing());
}

void testFallbackForgetDoesNotClaimSuccessWhenFallbackRecordRemains() {
  MemoryStore primary;
  MemoryStore fallback;
  orca::ConfigManager primaryManager(primary);
  CHECK(primaryManager.updateWifi(validWifi()));
  primary.failWrite = true;

  orca::FallbackConfigStore store(primary, fallback);
  orca::ConfigManager manager(store);
  CHECK(manager.load());
  CHECK(manager.updatePairing(validPairing()));
  fallback.failErase = true;

  CHECK(!manager.forget());
  CHECK(!fallback.bytes.empty());
}

void testPrimaryConfigCanForgetWhenUnusedFallbackIsUnavailable() {
  MemoryStore primary;
  MemoryStore fallback;
  orca::ConfigManager initial(primary);
  CHECK(initial.updateWifi(validWifi()));

  fallback.failErase = true;
  orca::FallbackConfigStore store(primary, fallback);
  orca::ConfigManager manager(store);
  CHECK(manager.load());
  CHECK(manager.forget());
  CHECK(!manager.active().hasWifi);
  CHECK(!manager.active().hasPairing);
  CHECK(primary.bytes.empty());
}

void testFallbackMountPlanFormatsOnlyVerifiedBlankPartition() {
  CHECK(orca::fallbackMountPlan(true, false) == orca::FallbackMountPlan::Mounted);
  CHECK(orca::fallbackMountPlan(false, true) == orca::FallbackMountPlan::FormatBlank);
  CHECK(orca::fallbackMountPlan(false, false) == orca::FallbackMountPlan::Refuse);
}

void testUiModelBoundsAndSanitization() {
  orca::ConsoleModel model;
  orca::ServerEvent snapshot;
  snapshot.type = orca::ServerEventType::Snapshot;
  snapshot.connected = true;
  snapshot.worktreeCount = 20;
  for (int i = 0; i < 20; ++i) {
    snapshot.rows.push_back({"worktree-" + std::to_string(i) + "\nsecret",
                             std::string(120, 'S'), "working"});
  }
  model.apply(snapshot);
  CHECK(model.rows().size() == orca::ConsoleModel::kMaximumRows);
  CHECK(model.rows().front().displayName.find('\n') == std::string::npos);
  CHECK(model.rows().front().workspaceStatus.size() == orca::ConsoleModel::kMaximumRowBytes);
  CHECK(model.connected());

  orca::ServerEvent notice;
  notice.type = orca::ServerEventType::Notify;
  notice.text = std::string(600, 'N');
  model.apply(notice);
  CHECK(model.notice().size() == orca::ConsoleModel::kMaximumNoticeBytes);

  orca::ServerEvent question;
  question.type = orca::ServerEventType::Question;
  question.questionId = "question-id";
  question.text = "Continue?";
  question.labels = {"Yes", "No", "Later", "Always", "Never", "ignored"};
  model.apply(question);
  CHECK(model.hasQuestion());
  CHECK(model.questionLabels().size() == orca::ConsoleModel::kMaximumLabels);
  CHECK(model.selectedLabel() == 0);
  model.moveQuestionSelection(-1);
  CHECK(model.selectedLabel() == 4);
  model.clearQuestion();
  CHECK(!model.hasQuestion());

  CHECK(model.prompt().empty());
  for (int i = 0; i < 600; ++i) model.appendPrompt('x');
  CHECK(model.prompt().size() == orca::ConsoleModel::kMaximumPromptBytes);
  model.backspacePrompt();
  CHECK(model.prompt().size() == orca::ConsoleModel::kMaximumPromptBytes - 1);
}

void testFrameParserAndNamedMessages() {
  orca::ServerEvent event;
  const std::string snapshot =
      "{\"version\":\"orca-cardputer/v1\",\"type\":\"snapshot\",\"snapshot\":{"
      "\"connected\":true,\"worktreeCount\":2,\"items\":["
      "{\"displayName\":\"alpha\",\"workspaceStatus\":\"clean\",\"agentState\":\"working\"},"
      "{\"displayName\":\"beta\",\"workspaceStatus\":\"dirty\"}]}}";
  CHECK(orca::parseServerFrame(snapshot, event));
  CHECK(event.type == orca::ServerEventType::Snapshot);
  CHECK(event.connected);
  CHECK(event.worktreeCount == 2);
  CHECK(event.rows.size() == 2);
  CHECK(event.rows[0].displayName == "alpha");

  CHECK(orca::parseServerFrame(
      "{\"version\":\"orca-cardputer/v1\",\"type\":\"notify\",\"text\":\"Build finished\"}", event));
  CHECK(event.type == orca::ServerEventType::Notify);
  CHECK(event.text == "Build finished");

  CHECK(orca::parseServerFrame(
      "{\"version\":\"orca-cardputer/v1\",\"type\":\"question.request\","
      "\"questionId\":\"question-1\",\"question\":\"Proceed?\",\"labels\":[\"Yes\",\"No\"]}", event));
  CHECK(event.type == orca::ServerEventType::Question);
  CHECK(event.labels.size() == 2);

  CHECK(!orca::parseServerFrame(std::string(4097, 'x'), event));
  CHECK(!orca::parseServerFrame(
      "{\"version\":\"orca-cardputer/v0\",\"type\":\"notify\",\"text\":\"x\"}", event));
  CHECK(!orca::parseServerFrame(
      "{\"version\":\"orca-cardputer/v1\",\"type\":\"terminal.output\",\"text\":\"x\"}", event));
  CHECK(!orca::parseServerFrame(
      "{\"version\":\"orca-cardputer/v1\",\"type\":\"notify\",\"text\":\"" +
      std::string(481, 'N') + "\"}", event));

  std::string malformedUtf8 =
      "{\"version\":\"orca-cardputer/v1\",\"type\":\"notify\",\"text\":\"";
  malformedUtf8.push_back(static_cast<char>(0xc3));
  malformedUtf8 += "\"}";
  CHECK(!orca::parseServerFrame(malformedUtf8, event));
  CHECK(!orca::parseServerFrame(
      "{\"version\":\"orca-cardputer/v1\",\"type\":\"question.request\","
      "\"questionId\":\"q\",\"question\":\"Proceed?\",\"labels\":[\"Yes\"]}", event));
}

void testDeviceMessageEncoding() {
  CHECK(orca::encodeHello() ==
        "{\"version\":\"orca-cardputer/v1\",\"type\":\"hello\"}");
  CHECK(orca::encodeState() ==
        "{\"version\":\"orca-cardputer/v1\",\"type\":\"state\"}");
  CHECK(orca::encodeQuestionAnswer("question-1", "Yes") ==
        "{\"version\":\"orca-cardputer/v1\",\"type\":\"question.answer\","
        "\"questionId\":\"question-1\",\"answer\":\"Yes\"}");
  CHECK(orca::encodePromptDraft("quote \" and slash \\") ==
        "{\"version\":\"orca-cardputer/v1\",\"type\":\"prompt.draft\","
        "\"text\":\"quote \\\" and slash \\\\\"}");
  CHECK(orca::encodePromptDraft(std::string(481, 'x')).empty());
}

void testReconnectBackoff() {
  orca::ReconnectBackoff backoff;
  CHECK(backoff.nextDelayMs() == 1000);
  CHECK(backoff.nextDelayMs() == 2000);
  CHECK(backoff.nextDelayMs() == 4000);
  for (int i = 0; i < 10; ++i) (void)backoff.nextDelayMs();
  CHECK(backoff.nextDelayMs() == 30000);
  backoff.reset();
  CHECK(backoff.nextDelayMs() == 1000);
  CHECK(!orca::ReconnectBackoff::deadlineReached(0xfffffff0u, 0x00000010u));
  CHECK(orca::ReconnectBackoff::deadlineReached(0x00000010u, 0xfffffff0u));
}

void testStoredWifiRetryPolicyHonorsUserCancellation() {
  orca::StoredWifiRetryPolicy policy;
  CHECK(policy.shouldAttempt(true, 1000, 1000));
  CHECK(!policy.shouldAttempt(false, 1000, 1000));
  CHECK(!policy.shouldAttempt(true, 999, 1000));

  policy.pause();
  CHECK(policy.paused());
  CHECK(!policy.shouldAttempt(true, 1000, 1000));

  policy.resume();
  CHECK(!policy.paused());
  CHECK(policy.shouldAttempt(true, 1000, 1000));
}

}  // namespace

int main() {
  testProvisioningValidation();
  testEndpointContract();
  testBlobChecksumAndVersion();
  testFailedUpdatesPreserveActiveConfig();
  testLoadFailsClosed();
  testFallbackStorePersistsCombinedConfigWhenPrimaryRejectsLargeUpdate();
  testFallbackForgetDoesNotClaimSuccessWhenFallbackRecordRemains();
  testPrimaryConfigCanForgetWhenUnusedFallbackIsUnavailable();
  testFallbackMountPlanFormatsOnlyVerifiedBlankPartition();
  testUiModelBoundsAndSanitization();
  testFrameParserAndNamedMessages();
  testDeviceMessageEncoding();
  testReconnectBackoff();
  testStoredWifiRetryPolicyHonorsUserCancellation();
  if (failures != 0) {
    std::cerr << failures << " host test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "PASS: 12 Orca Buddy host behavior groups\n";
  return EXIT_SUCCESS;
}
