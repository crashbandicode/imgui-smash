#include "font_setup.hpp"

#include "imgui_internal.h"
#include "JetBrainsMonoNL-Regular.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace ImguiFontSetup {
namespace {

std::atomic<int> g_sharedSources{0};
std::atomic<int> g_rejectedSources{0};
std::atomic<int> g_admitted{0};
std::atomic<int> g_refusals{0};
std::atomic<int> g_fallbackChar{0};
Limits g_limits = kDefaultLimits;

uint16_t be16(const unsigned char *p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

uint32_t be32(const unsigned char *p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

struct Table {
  uint32_t offset;
  uint32_t length;
};

bool findTable(const unsigned char *font, size_t size, size_t start, const char *tag, Table *out) {
  if (start + 12 > size) {
    return false;
  }
  const uint16_t tables = be16(font + start + 4);
  if (start + 12 + static_cast<size_t>(tables) * 16 > size) {
    return false;
  }
  for (uint16_t i = 0; i < tables; ++i) {
    const unsigned char *record = font + start + 12 + static_cast<size_t>(i) * 16;
    if (std::memcmp(record, tag, 4) != 0) {
      continue;
    }
    const uint32_t offset = be32(record + 8);
    const uint32_t length = be32(record + 12);
    if (static_cast<size_t>(offset) + length > size) {
      return false;
    }
    *out = {offset, length};
    return true;
  }
  return false;
}

bool unicodeCmap(const unsigned char *font, Table cmap) {
  if (cmap.length < 4) {
    return false;
  }
  const unsigned char *base = font + cmap.offset;
  const uint16_t count = be16(base + 2);
  if (4 + static_cast<size_t>(count) * 8 > cmap.length) {
    return false;
  }
  for (uint16_t i = 0; i < count; ++i) {
    const unsigned char *record = base + 4 + static_cast<size_t>(i) * 8;
    const uint16_t platform = be16(record);
    const uint16_t encoding = be16(record + 2);
    const uint32_t offset = be32(record + 4);
    const bool unicode = platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10));
    if (unicode && offset + 4u <= cmap.length) {
      return true;
    }
  }
  return false;
}

// stb_truetype's own loader with one addition: a shared-font glyph is rasterized only while the limits allow it.
// A refused glyph makes ImGui try the next source and then draw the fallback glyph for that character.
bool budgetLoadGlyph(ImFontAtlas *atlas, ImFontConfig *src, ImFontBaked *baked, void *loaderData,
                     ImWchar codepoint, ImFontGlyph *outGlyph, float *outAdvance) {
  const ImFontLoader *stb = ImFontAtlasGetFontLoaderForStbTruetype();
  if (!stb->FontSrcContainsGlyph(atlas, src, codepoint)) {
    return false;
  }
  const ImFontAtlasBuilder *builder = atlas->Builder;
  const long long live =
      builder != nullptr ? static_cast<long long>(builder->RectsPackedSurface) - builder->RectsDiscardedSurface : 0;
  if (!admitGlyph(g_admitted.load(std::memory_order_relaxed), live, baked->Size, g_limits)) {
    g_refusals.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  const bool loaded = stb->FontBakedLoadGlyph(atlas, src, baked, loaderData, codepoint, outGlyph, outAdvance);
  if (loaded && outGlyph != nullptr) {
    g_admitted.fetch_add(1, std::memory_order_relaxed);
  }
  return loaded;
}

const ImFontLoader *budgetLoader() {
  static ImFontLoader loader;
  static bool ready = false;
  if (!ready) {
    loader = *ImFontAtlasGetFontLoaderForStbTruetype();
    loader.Name = "stb_truetype+budget";
    loader.LoaderInit = nullptr;
    loader.LoaderShutdown = nullptr;
    loader.FontBakedLoadGlyph = budgetLoadGlyph;
    ready = true;
  }
  return &loader;
}

}  // namespace

bool validFont(const void *data, size_t size) {
  if (data == nullptr || size < 12 || size > 0x7fffffff) {
    return false;
  }
  const auto *font = static_cast<const unsigned char *>(data);
  size_t start = 0;
  if (std::memcmp(font, "ttcf", 4) == 0) {
    if (size < 16 || be32(font + 8) == 0) {
      return false;
    }
    start = be32(font + 12);
    if (start + 12 > size) {
      return false;
    }
  }
  const uint32_t version = be32(font + start);
  if (version != 0x00010000u && std::memcmp(font + start, "true", 4) != 0 &&
      std::memcmp(font + start, "OTTO", 4) != 0) {
    return false;
  }
  Table cmap{}, head{}, hhea{}, hmtx{}, glyf{}, loca{}, cff{};
  if (!findTable(font, size, start, "cmap", &cmap) || !findTable(font, size, start, "head", &head) ||
      !findTable(font, size, start, "hhea", &hhea) || !findTable(font, size, start, "hmtx", &hmtx)) {
    return false;
  }
  if (head.length < 54 || hhea.length < 36) {
    return false;
  }
  const bool outlines = (findTable(font, size, start, "glyf", &glyf) && findTable(font, size, start, "loca", &loca)) ||
                        findTable(font, size, start, "CFF ", &cff);
  return outlines && unicodeCmap(font, cmap);
}

bool admitGlyph(int admitted, long long livePackedSurface, float fontSize, const Limits &limits) {
  if (admitted >= limits.glyphBudget) {
    return false;
  }
  // Reserve a glyph up to two ems wide plus padding.
  const long long side = static_cast<long long>(fontSize * 2.0f) + 2;
  const long long capacity = static_cast<long long>(limits.texMaxWidth) * limits.texMaxHeight;
  return livePackedSurface + side * side <= static_cast<long long>(static_cast<double>(capacity) * limits.fillLimit);
}

ImFont *setup(ImFontAtlas *atlas, const SharedFont *shared, int count, const Limits &limits) {
  g_limits = limits;
  g_sharedSources.store(0, std::memory_order_relaxed);
  g_rejectedSources.store(0, std::memory_order_relaxed);
  g_admitted.store(0, std::memory_order_relaxed);
  g_refusals.store(0, std::memory_order_relaxed);

  atlas->TexDesiredFormat = ImTextureFormat_Alpha8;
  atlas->TexMinWidth = limits.texMinWidth;
  atlas->TexMinHeight = limits.texMinHeight;
  atlas->TexMaxWidth = limits.texMaxWidth;
  atlas->TexMaxHeight = limits.texMaxHeight;

  ImFont *font = atlas->AddFontFromMemoryCompressedTTF(JetBrainsMonoNL_compressed_data,
                                                       JetBrainsMonoNL_compressed_size, kFontSize);
  if (font == nullptr) {
    return nullptr;
  }
  // One 18 px bake. Other sizes scale it, as the legacy atlas did, so text that pushes another size adds no bake.
  font->Flags |= ImFontFlags_LockBakedSizes;

  const int capped = count < kMaxSharedFonts ? count : kMaxSharedFonts;
  for (int i = 0; i < capped; ++i) {
    if (!validFont(shared[i].data, shared[i].size)) {
      g_rejectedSources.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    ImFontConfig config;
    config.FontData = const_cast<void *>(shared[i].data);
    config.FontDataSize = static_cast<int>(shared[i].size);
    config.FontDataOwnedByAtlas = false;
    config.MergeMode = true;
    config.OversampleH = 1;
    config.OversampleV = 1;
    config.FontLoader = budgetLoader();
    std::snprintf(config.Name, sizeof(config.Name), "shared font %d", i);
    if (atlas->AddFont(&config) != nullptr) {
      g_sharedSources.fetch_add(1, std::memory_order_relaxed);
    } else {
      g_rejectedSources.fetch_add(1, std::memory_order_relaxed);
    }
  }
  if (font->IsGlyphInFont(IM_UNICODE_CODEPOINT_INVALID)) {
    font->FallbackChar = IM_UNICODE_CODEPOINT_INVALID;
  }
  g_fallbackChar.store(font->FallbackChar, std::memory_order_relaxed);
  return font;
}

Stats stats() {
  return {g_sharedSources.load(std::memory_order_relaxed), g_rejectedSources.load(std::memory_order_relaxed),
          g_admitted.load(std::memory_order_relaxed), g_refusals.load(std::memory_order_relaxed),
          g_fallbackChar.load(std::memory_order_relaxed)};
}

}  // namespace ImguiFontSetup
