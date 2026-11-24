#pragma once

namespace ImguiFontRetry {

constexpr bool due(unsigned frames, bool ready) {
  return !ready && frames != 0 && frames % 60u == 0;
}

}  // namespace ImguiFontRetry
