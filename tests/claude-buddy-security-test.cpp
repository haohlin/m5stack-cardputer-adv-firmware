#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "bridge_config_storage.h"
#include "security_utils.h"

struct BridgeRecordFixture {
  char endpoint[48];
  char token[33];
  char ca[80];
};

int main() {
  using AppendPathInterface = bool (*)(char (*)[32], uint8_t&, const char*);
  using ChunkFitsInterface = bool (*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
  AppendPathInterface appendPath = static_cast<AppendPathInterface>(&buddyAppendPath);
  ChunkFitsInterface chunkFits = static_cast<ChunkFitsInterface>(&buddyChunkFits);

  assert(buddySafePathComponent("bufo", 24));
  assert(buddySafePathComponent("idle_0.gif", 48));
  assert(buddySafePathComponent("manifest.json", 48));
  assert(!buddySafePathComponent("", 24));
  assert(!buddySafePathComponent(".", 24));
  assert(!buddySafePathComponent("..", 24));
  assert(!buddySafePathComponent("../victim", 24));
  assert(!buddySafePathComponent("nested/file.gif", 48));
  assert(!buddySafePathComponent("nested\\file.gif", 48));
  assert(!buddySafePathComponent("abcdefghijklmnopqrstuvwxyz", 24));

  char paths[2][32] = {};
  uint8_t count = 0;
  assert((buddyAppendPath<2, 32>(paths, count, "idle.gif")));
  assert((buddyAppendPath<2, 32>(paths, count, "busy.gif")));
  assert((!buddyAppendPath<2, 32>(paths, count, "overflow.gif")));
  assert(count == 2);
  assert(strcmp(paths[0], "idle.gif") == 0);

  char exactPaths[32][32] = {};
  uint8_t exactCount = 0;
  assert(appendPath(exactPaths, exactCount, "manifest.json"));
  assert(exactCount == 1);

  assert(buddyFormattedLengthFits(319, 320));
  assert(!buddyFormattedLengthFits(320, 320));
  assert(!buddyFormattedLengthFits(-1, 320));

  assert(buddyTransferFits(1, 4097, 4096));
  assert(!buddyTransferFits(0, 4097, 4096));
  assert(!buddyTransferFits(2, 4097, 4096));
  assert(buddyChunkFits(100, 80, 200, 150, 20));
  assert(!buddyChunkFits(100, 80, 200, 150, 21));
  assert(!buddyChunkFits(100, 80, 200, 190, 20));
  assert(chunkFits(100, 80, 200, 150, 20));

  assert(buddyPairingPasskey(0) == 100000);
  assert(buddyPairingPasskey(UINT32_MAX) >= 100000);
  assert(buddyPairingPasskey(UINT32_MAX) <= 999999);

  assert(buddyBridgeTokenAllowed("Az09_-bcDE12fgHI34jkLM56noPQ78rs"));
  assert(!buddyBridgeTokenAllowed("aaaaaaaaaaaaaaaaaaaaaaaa"));
  assert(!buddyBridgeTokenAllowed("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
  assert(!buddyBridgeTokenAllowed("Az09_-bcDE12fgHI34jkLM56noPQ78r+"));

  BridgeRecordFixture config = {};
  strcpy(config.endpoint, "wss://bridge.example/device");
  strcpy(config.token, "Az09_-bcDE12fgHI34jkLM56noPQ78rs");
  strcpy(config.ca, "-----BEGIN CERTIFICATE-----public-----END CERTIFICATE-----");
  auto record = buddyBridgeConfigRecordMake(config);
  BridgeRecordFixture loaded = {};
  assert(buddyBridgeConfigRecordExtract(record, &loaded));
  assert(strcmp(loaded.endpoint, config.endpoint) == 0);
  assert(strcmp(loaded.token, config.token) == 0);
  assert(strcmp(loaded.ca, config.ca) == 0);

  auto mixedToken = record;
  mixedToken.config.token[0] ^= 1;
  assert(!buddyBridgeConfigRecordExtract(mixedToken, &loaded));
  auto mixedCa = record;
  mixedCa.config.ca[10] ^= 1;
  assert(!buddyBridgeConfigRecordExtract(mixedCa, &loaded));
  auto malformed = record;
  malformed.payloadSize--;
  assert(!buddyBridgeConfigRecordExtract(malformed, &loaded));
  return 0;
}
