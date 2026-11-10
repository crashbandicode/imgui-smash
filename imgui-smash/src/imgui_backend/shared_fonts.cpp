#include "shared_fonts.hpp"

#include "logger/Logger.hpp"
#include "nn/pl.h"

namespace ImguiSharedFonts {

int query(ImguiFontSetup::SharedFont *out, int capacity) {
  static const nn::pl::SharedFontType kOrder[] = {
      nn::pl::STANDARD,     nn::pl::CHINESE_SIMPLIFIED, nn::pl::EXT_CHINESE_SIMPLIFIED,
      nn::pl::CHINESE_TRAD, nn::pl::KOREAN,             nn::pl::NN_EXT,
  };
  int count = 0;
  for (nn::pl::SharedFontType type : kOrder) {
    if (count >= capacity) {
      break;
    }
    nn::pl::RequestSharedFontLoad(type);
    if (nn::pl::GetSharedFontLoadState(type) != nn::pl::SharedFontLoadState_Loaded) {
      Logger::log("Shared font %d not loaded; skipped.\n", static_cast<int>(type));
      continue;
    }
    const void *address = nn::pl::GetSharedFontAddress(type);
    const size_t size = nn::pl::GetSharedFontSize(type);
    Logger::log("Shared font %d: %p, %lu bytes.\n", static_cast<int>(type), address, static_cast<unsigned long>(size));
    if (address == nullptr || size == 0) {
      continue;
    }
    out[count++] = {address, size};
  }
  return count;
}

}  // namespace ImguiSharedFonts
