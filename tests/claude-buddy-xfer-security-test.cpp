#define BUDDY_XFER_HOST_TEST 1

#include "xfer.h"

namespace {

#define CHECK(condition) do { \
  if (!(condition)) { \
    std::fprintf(stderr, "%s:%d: check failed: %s\n", __func__, __LINE__, #condition); \
    return false; \
  } \
} while (0)

void resetHarness() {
  _xFile = File();
  _xExpected = 0;
  _xWritten = 0;
  _xCharName[0] = 0;
  _xActive = false;
  _xBridgeConfig = false;
  _xFailed = false;
  _xTotal = 0;
  _xTotalWritten = 0;
  _xLastActivityMs = 0;
  LittleFS.reset();
  fakeMillis = 1000;
  fakeSerialOutput.clear();
  fakeSettings = FakeSettings{};
  fakeBridgeConfigSaved = false;
}

bool ackIs(const char* command, bool ok) {
  const std::string expected = std::string("\"ack\":\"") + command +
                               "\",\"ok\":" + (ok ? "true" : "false");
  return fakeSerialOutput.find(expected) != std::string::npos;
}

void send(JsonDocument& document) {
  fakeSerialOutput.clear();
  if (!xferCommand(document)) std::abort();
}

JsonDocument command(const char* name) {
  JsonDocument document;
  document.setString("cmd", name);
  return document;
}

JsonDocument begin(const char* name, uint32_t total) {
  JsonDocument document = command("char_begin");
  document.setString("name", name);
  document.setUInt("total", total);
  return document;
}

JsonDocument file(const char* path, uint32_t size) {
  JsonDocument document = command("file");
  document.setString("path", path);
  document.setUInt("size", size);
  return document;
}

JsonDocument chunk(const char* base64) {
  JsonDocument document = command("chunk");
  document.setString("d", base64);
  return document;
}

bool testUnsafeNameAndPathNeverCreateFiles() {
  resetHarness();
  auto unsafeBegin = begin("../escape", 1);
  send(unsafeBegin);
  CHECK(ackIs("char_begin", false));
  CHECK(LittleFS.files.empty());
  CHECK(!LittleFS.exists("/characters"));

  auto safeBegin = begin("safe-pet", 1);
  send(safeBegin);
  CHECK(ackIs("char_begin", true));
  auto unsafeFile = file("../escape.gif", 1);
  send(unsafeFile);
  CHECK(ackIs("file", false));
  CHECK(!LittleFS.exists("/characters/safe-pet"));
  CHECK(LittleFS.files.empty());
  return true;
}

bool testZeroAndOversizedTotalsRejectBeforeFilesystemMutation() {
  resetHarness();
  auto zeroCharacter = begin("safe-pet", 0);
  send(zeroCharacter);
  CHECK(ackIs("char_begin", false));
  CHECK(!LittleFS.exists("/characters/safe-pet"));

  LittleFS.seedFile("/bridge/keep.json", "existing");
  auto zeroBridge = begin("bridge-config", 0);
  send(zeroBridge);
  CHECK(ackIs("char_begin", false));
  CHECK(LittleFS.readText("/bridge/keep.json") == "existing");
  CHECK(!xferActive());

  auto oversizedBridge = begin("bridge-config", 4097);
  send(oversizedBridge);
  CHECK(ackIs("char_begin", false));
  CHECK(LittleFS.readText("/bridge/keep.json") == "existing");
  CHECK(!xferActive());

  auto disguisedBridge = begin("safe-pet", 4097);
  send(disguisedBridge);
  CHECK(ackIs("char_begin", true));
  auto oversizedBridgeFile = file("bridge.json", 4097);
  send(oversizedBridgeFile);
  CHECK(ackIs("file", false));
  CHECK(LittleFS.readText("/bridge/keep.json") == "existing");
  CHECK(!LittleFS.exists("/characters/safe-pet"));
  CHECK(!xferActive());

  LittleFS.capacity = 8192;
  auto oversizedCharacter = begin("safe-pet", 5000);
  send(oversizedCharacter);
  CHECK(ackIs("char_begin", false));
  CHECK(!LittleFS.exists("/characters/safe-pet"));
  return true;
}

bool testOverflowChunkAbortsAndRemovesPartialCharacter() {
  resetHarness();
  auto start = begin("safe-pet", 1);
  send(start);
  auto openFile = file("idle.gif", 1);
  send(openFile);
  auto overflow = chunk("QUJD");
  send(overflow);
  CHECK(ackIs("chunk", false));
  CHECK(!xferActive());
  CHECK(!LittleFS.exists("/characters/safe-pet/idle.gif"));
  CHECK(!LittleFS.exists("/characters/safe-pet"));
  return true;
}

bool testMalformedBridgeJsonIsRejectedAndRemoved() {
  resetHarness();
  auto start = begin("bridge-config", 8);
  send(start);
  CHECK(ackIs("char_begin", true));
  auto openFile = file("bridge.json", 8);
  send(openFile);
  CHECK(ackIs("file", true));
  auto data = chunk("bm90LWpzb24=");
  send(data);
  CHECK(ackIs("chunk", true));
  auto fileEnd = command("file_end");
  send(fileEnd);
  CHECK(ackIs("file_end", true));
  auto characterEnd = command("char_end");
  send(characterEnd);
  CHECK(ackIs("char_end", false));
  CHECK(!fakeBridgeConfigSaved);
  CHECK(!LittleFS.exists("/bridge/bridge.json"));
  return true;
}

bool testShortWriteAbortsAndRemovesPartialCharacter() {
  resetHarness();
  auto start = begin("safe-pet", 3);
  send(start);
  auto openFile = file("idle.gif", 3);
  send(openFile);
  LittleFS.nextWriteLimit = 2;
  auto data = chunk("QUJD");
  send(data);
  CHECK(ackIs("chunk", false));
  CHECK(!xferActive());
  CHECK(!LittleFS.exists("/characters/safe-pet/idle.gif"));
  CHECK(!LittleFS.exists("/characters/safe-pet"));
  return true;
}

bool testTimeoutAbortsAndRemovesPartialCharacter() {
  resetHarness();
  auto start = begin("safe-pet", 1);
  send(start);
  auto openFile = file("idle.gif", 1);
  send(openFile);
  auto data = chunk("QQ==");
  send(data);
  CHECK(LittleFS.exists("/characters/safe-pet/idle.gif"));
  fakeMillis += 30001;
  xferTick();
  CHECK(!xferActive());
  CHECK(!LittleFS.exists("/characters/safe-pet/idle.gif"));
  CHECK(!LittleFS.exists("/characters/safe-pet"));
  return true;
}

bool testValidBridgeConfigStillSaves() {
  resetHarness();
  static const char validJson[] = "{\"host\":\"h\",\"token\":\"t\"}";
  static_assert(sizeof(validJson) - 1 == 24, "fixture length changed");
  auto start = begin("bridge-config", sizeof(validJson) - 1);
  send(start);
  CHECK(ackIs("char_begin", true));
  auto openFile = file("bridge.json", sizeof(validJson) - 1);
  send(openFile);
  auto data = chunk("eyJob3N0IjoiaCIsInRva2VuIjoidCJ9");
  send(data);
  auto fileEnd = command("file_end");
  send(fileEnd);
  CHECK(ackIs("file_end", true));
  auto characterEnd = command("char_end");
  send(characterEnd);
  CHECK(ackIs("char_end", true));
  CHECK(fakeBridgeConfigSaved);
  CHECK(!settings().wifi);
  return true;
}

}  // namespace

int main() {
  struct TestCase {
    const char* name;
    bool (*run)();
  };
  const TestCase tests[] = {
      {"unsafe name and path", testUnsafeNameAndPathNeverCreateFiles},
      {"zero and oversized totals", testZeroAndOversizedTotalsRejectBeforeFilesystemMutation},
      {"overflow chunk", testOverflowChunkAbortsAndRemovesPartialCharacter},
      {"malformed bridge JSON", testMalformedBridgeJsonIsRejectedAndRemoved},
      {"short write", testShortWriteAbortsAndRemovesPartialCharacter},
      {"timeout", testTimeoutAbortsAndRemovesPartialCharacter},
      {"valid bridge config", testValidBridgeConfigStillSaves},
  };

  int failures = 0;
  for (const auto& test : tests) {
    if (!test.run()) {
      std::fprintf(stderr, "FAIL: %s\n", test.name);
      ++failures;
    }
  }
  return failures == 0 ? 0 : 1;
}
