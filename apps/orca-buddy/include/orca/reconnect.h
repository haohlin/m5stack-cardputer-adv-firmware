#pragma once

#include <cstdint>

namespace orca {

class ReconnectBackoff {
 public:
  void reset();
  std::uint32_t nextDelayMs();
  static bool deadlineReached(std::uint32_t now, std::uint32_t deadline);

 private:
  std::uint32_t delayMs_ = 1000;
};

}  // namespace orca
