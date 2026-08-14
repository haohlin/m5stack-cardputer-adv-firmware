#include "orca/reconnect.h"

#include <algorithm>
#include <cstdint>

namespace orca {

void ReconnectBackoff::reset() { delayMs_ = 1000; }

std::uint32_t ReconnectBackoff::nextDelayMs() {
  const std::uint32_t result = delayMs_;
  delayMs_ = std::min<std::uint32_t>(delayMs_ * 2u, 30000u);
  return result;
}

bool ReconnectBackoff::deadlineReached(std::uint32_t now,
                                       std::uint32_t deadline) {
  return static_cast<std::int32_t>(now - deadline) >= 0;
}

}  // namespace orca
