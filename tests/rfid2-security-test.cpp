#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "security_utils.h"

int main() {
  const char* confirmed = "write-block 4 00112233445566778899aabbccddeeff confirm";
  const size_t bodyLength = rfidConfirmedCommandLength(confirmed);
  assert(bodyLength == strlen("write-block 4 00112233445566778899aabbccddeeff"));
  assert(strncmp(confirmed, "write-block 4 00112233445566778899aabbccddeeff", bodyLength) == 0);
  assert(rfidConfirmedCommandLength("write-block 4 00") == 0);
  assert(rfidConfirmedCommandLength("write-block 4 00 confirmation") == 0);
  assert(rfidConfirmedCommandLength("write-block 4 00 xconfirm") == 0);
  assert(rfidConfirmedCommandLength("write-block 4 00 confirm  ") == strlen("write-block 4 00"));
  assert(rfidConfirmedCommandLength("write 1 confirmation") == 0);
  assert(rfidConfirmedCommandLength("write 1 xconfirm") == 0);

  const uint32_t started = 1000;
  const uint32_t deadline = rfidWriteArmDeadline(started);
  assert(rfidArmStillValid(started + 7999, deadline));
  assert(!rfidArmStillValid(started + 8000, deadline));
  assert(!rfidArmStillValid(started + 9000, deadline));
  const uint32_t wrappedStart = UINT32_MAX - 4000;
  const uint32_t wrappedDeadline = rfidWriteArmDeadline(wrappedStart);
  assert(rfidArmStillValid(wrappedStart + 1, wrappedDeadline));
  assert(!rfidArmStillValid(wrappedDeadline, wrappedDeadline));
  assert(rfidSerialDestructiveArmValid(started + 7999, deadline, true, true, "04 AA BB CC", "04 AA BB CC"));
  assert(!rfidSerialDestructiveArmValid(started + 7999, deadline, false, true, "04 AA BB CC", "04 AA BB CC"));
  assert(!rfidSerialDestructiveArmValid(started + 7999, deadline, true, false, "04 AA BB CC", "04 AA BB CC"));
  assert(!rfidSerialDestructiveArmValid(started + 7999, deadline, true, true, "04 AA BB CC", "04 11 22 33"));
  assert(!rfidSerialDestructiveArmValid(started + 7999, deadline, true, true, "", "04 AA BB CC"));

  assert(rfidPersistFileAllowed(4096, RFID_MAX_SLOT_FILE_BYTES));
  assert(!rfidPersistFileAllowed(RFID_MAX_SLOT_FILE_BYTES + 1, RFID_MAX_SLOT_FILE_BYTES));
  assert(rfidPersistLineAllowed(RFID_MAX_PERSIST_LINE_CHARS));
  assert(!rfidPersistLineAllowed(RFID_MAX_PERSIST_LINE_CHARS + 1));
  assert(rfidKeyCountAllowsInsert(RFID_MAX_MIFARE_KEYS - 1));
  assert(!rfidKeyCountAllowsInsert(RFID_MAX_MIFARE_KEYS));
  return 0;
}
