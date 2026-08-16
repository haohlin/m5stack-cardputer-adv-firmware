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

// Saved network credentials remain intact when the owner cancels a reconnect.
// The next successful/manual connection resumes normal automatic retries.
class StoredWifiRetryPolicy {
 public:
  void pause();
  void resume();
  bool paused() const;
  bool shouldAttempt(bool hasStoredWifi, std::uint32_t now,
                     std::uint32_t deadline) const;

 private:
  bool paused_ = false;
};

}  // namespace orca
