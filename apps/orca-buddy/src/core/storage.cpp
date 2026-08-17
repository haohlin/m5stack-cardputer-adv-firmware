#include "orca/storage.h"

namespace orca {

FallbackMountPlan fallbackMountPlan(bool mountSucceeded, bool partitionBlank) {
  if (mountSucceeded) return FallbackMountPlan::Mounted;
  return partitionBlank ? FallbackMountPlan::FormatBlank : FallbackMountPlan::Refuse;
}

}  // namespace orca
