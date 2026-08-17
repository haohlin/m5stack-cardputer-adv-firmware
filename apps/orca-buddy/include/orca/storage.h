#pragma once

namespace orca {

enum class FallbackMountPlan {
  Mounted,
  FormatBlank,
  Refuse,
};

FallbackMountPlan fallbackMountPlan(bool mountSucceeded, bool partitionBlank);

}  // namespace orca
