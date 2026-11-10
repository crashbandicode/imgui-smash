#include "font_setup.hpp"

#include "imgui_internal.h"
#include "JetBrainsMonoNL-Regular.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace ImguiFontSetup {
namespace {

// imgui_draw.cpp's IM_FONTGLYPH_INDEX_UNUSED and IM_FONTGLYPH_INDEX_NOT_FOUND (file-local macros there).
constexpr ImU16 kIndexUnused = 0xFFFF;
constexpr ImU16 kIndexNotFound = 0xFFFE;
// A static glyph, or a dynamic one without pixels: not tracked on screen.
constexpr int kUntracked = -2;
// A dynamic glyph not drawn since it was loaded.
constexpr int kNeverDrawn = -1;
constexpr int kCell = 32;
constexpr int kSnapshotCap = 256;

std::atomic<int> g_sharedSources{0};
std::atomic<int> g_rejectedSources{0};
std::atomic<int> g_fallbackChar{0};
std::atomic<int> g_staticGlyphs{0};
std::atomic<int> g_dynamicGlyphs{0};
std::atomic<int> g_dynamicVisible{0};
std::atomic<long long> g_dynamicLoads{0};
std::atomic<long long> g_refusals{0};
std::atomic<long long> g_rebuilds{0};
std::atomic<long long> g_misplaced{0};
std::atomic<long long> g_indexBytes{0};

// The dynamic set as of the last frameBoundary(), behind a sequence counter (odd while written).
std::atomic<unsigned> g_snapshotSeq{0};
std::atomic<int> g_snapshotCount{0};
std::atomic<uint32_t> g_snapshot[kSnapshotCap];

// Render-thread state.
Limits g_limits = kDefaultLimits;
ImFont *g_font = nullptr;
long long g_boundary = 0;
long long g_lastRebuild = 0;
bool g_refusedSinceRebuild = false;
ImGuiID g_bakeId = 0;
const ImFontBaked *g_bake = nullptr;
int g_snapshotGlyphs = -1;
ImVector<int> g_seen;       // per glyph index of the bake: kUntracked, kNeverDrawn or the boundary it was last drawn
ImVector<int> g_cellHead;   // dynamic glyphs by 32 px atlas cell, for the draw-data scan
ImVector<int> g_cellNext;
ImVector<int> g_cellGlyph;

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

bool isDynamic(const ImFont *font, unsigned int c) {
  return !isStaticCodepoint(c) && c != font->FallbackChar && c != font->EllipsisChar;
}

long long livePacked(const ImFontAtlas *atlas) {
  const ImFontAtlasBuilder *builder = atlas->Builder;
  return builder != nullptr ? static_cast<long long>(builder->RectsPackedSurface) - builder->RectsDiscardedSurface
                            : 0;
}

// stb_truetype's own loader, with the fill guard on dynamic glyphs. A refused glyph makes ImGui try the next source
// and then draw the fallback glyph for that character.
bool guardedLoadGlyph(ImFontAtlas *atlas, ImFontConfig *src, ImFontBaked *baked, void *loaderData,
                      ImWchar codepoint, ImFontGlyph *outGlyph, float *outAdvance) {
  const ImFontLoader *stb = ImFontAtlasGetFontLoaderForStbTruetype();
  if (!stb->FontSrcContainsGlyph(atlas, src, codepoint)) {
    return false;
  }
  // outGlyph is null when CalcTextSize() asks for the advance only (LoadNoRenderOnLayout): no atlas space.
  const bool dynamic = isDynamic(baked->OwnerFont, codepoint);
  if (dynamic && outGlyph != nullptr && !admitDynamic(livePacked(atlas), baked->Size, g_limits)) {
    g_refusals.fetch_add(1, std::memory_order_relaxed);
    g_refusedSinceRebuild = true;
    return false;
  }
  const bool loaded = stb->FontBakedLoadGlyph(atlas, src, baked, loaderData, codepoint, outGlyph, outAdvance);
  if (loaded && dynamic && outGlyph != nullptr) {
    g_dynamicLoads.fetch_add(1, std::memory_order_relaxed);
  }
  return loaded;
}

const ImFontLoader *guardedLoader() {
  static ImFontLoader loader;
  static bool ready = false;
  if (!ready) {
    loader = *ImFontAtlasGetFontLoaderForStbTruetype();
    loader.Name = "stb_truetype+static/dynamic";
    loader.LoaderInit = nullptr;
    loader.LoaderShutdown = nullptr;
    loader.FontBakedLoadGlyph = guardedLoadGlyph;
    ready = true;
  }
  return &loader;
}

ImFontBaked *mainBake(ImFontAtlas *atlas) {
  if (g_font == nullptr || atlas->Builder == nullptr) {
    return nullptr;
  }
  ImFontAtlasBuilder *builder = atlas->Builder;
  for (int i = 0; i < builder->BakedPool.Size; ++i) {
    ImFontBaked *baked = &builder->BakedPool[i];
    if (baked->OwnerFont == g_font && !baked->WantDestroy && baked->Size == kFontSize) {
      return baked;
    }
  }
  return nullptr;
}

// Keeps g_seen aligned with the bake's glyph table: entries are appended as ImGui loads glyphs.
void trackNewGlyphs(const ImFontBaked *baked) {
  if (baked != g_bake || baked->BakedId != g_bakeId || baked->Glyphs.Size < g_seen.Size) {
    g_seen.resize(0);
    g_bake = baked;
    g_bakeId = baked->BakedId;
  }
  for (int i = g_seen.Size; i < baked->Glyphs.Size; ++i) {
    const ImFontGlyph &glyph = baked->Glyphs[i];
    const bool tracked = isDynamic(g_font, glyph.Codepoint) && glyph.PackId != ImFontAtlasRectId_Invalid;
    g_seen.push_back(tracked ? kNeverDrawn : kUntracked);
  }
}

// Marks the tracked glyphs whose atlas rectangle a vertex of `drawData` samples (font-atlas commands only).
void scanDrawData(const ImFontAtlas *atlas, const ImFontBaked *baked, const ImDrawData *drawData) {
  const ImTextureData *tex = atlas->TexData;
  const int width = tex->Width, height = tex->Height;
  const int cols = (width + kCell - 1) / kCell, rows = (height + kCell - 1) / kCell;
  g_cellHead.resize(cols * rows);
  std::fill(g_cellHead.begin(), g_cellHead.end(), -1);
  g_cellNext.resize(0);
  g_cellGlyph.resize(0);
  for (int i = 0; i < g_seen.Size; ++i) {
    if (g_seen[i] == kUntracked) {
      continue;
    }
    const ImFontGlyph &glyph = baked->Glyphs[i];
    const int x0 = std::max(0, static_cast<int>(glyph.U0 * width) / kCell);
    const int x1 = std::min(cols - 1, static_cast<int>(glyph.U1 * width) / kCell);
    const int y0 = std::max(0, static_cast<int>(glyph.V0 * height) / kCell);
    const int y1 = std::min(rows - 1, static_cast<int>(glyph.V1 * height) / kCell);
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        g_cellNext.push_back(g_cellHead[y * cols + x]);
        g_cellGlyph.push_back(i);
        g_cellHead[y * cols + x] = g_cellNext.Size - 1;
      }
    }
  }
  for (const ImDrawList *list : drawData->CmdLists) {
    for (const ImDrawCmd &cmd : list->CmdBuffer) {
      if (cmd.UserCallback != nullptr || cmd.TexRef._TexData != tex) {
        continue;
      }
      for (unsigned int k = cmd.IdxOffset; k < cmd.IdxOffset + cmd.ElemCount; ++k) {
        const ImDrawVert &vert = list->VtxBuffer[static_cast<int>(cmd.VtxOffset + list->IdxBuffer[static_cast<int>(k)])];
        const float px = vert.uv.x * width, py = vert.uv.y * height;
        if (px < 0.0f || py < 0.0f || px >= width || py >= height) {
          continue;
        }
        for (int e = g_cellHead[static_cast<int>(py) / kCell * cols + static_cast<int>(px) / kCell]; e >= 0;
             e = g_cellNext[e]) {
          const ImFontGlyph &glyph = baked->Glyphs[g_cellGlyph[e]];
          // Clipped quads sample inside the rectangle; padding keeps neighbours a pixel apart.
          if (px >= glyph.U0 * width - 0.5f && px <= glyph.U1 * width + 0.5f && py >= glyph.V0 * height - 0.5f &&
              py <= glyph.V1 * height + 0.5f) {
            g_seen[g_cellGlyph[e]] = static_cast<int>(g_boundary);
            break;
          }
        }
      }
    }
  }
}

// Drops every dynamic glyph of `baked`, keeps the static set with its table and index entries, and repacks the
// atlas into a new texture without the dropped rectangles.
void rebuild(ImFontAtlas *atlas, ImFontBaked *baked) {
  ImVector<ImFontGlyph> kept;
  kept.reserve(baked->Glyphs.Size);
  int fallback = -1;
  unsigned int top = 0;
  for (int i = 0; i < baked->Glyphs.Size; ++i) {
    const ImFontGlyph &glyph = baked->Glyphs[i];
    if (isDynamic(g_font, glyph.Codepoint)) {
      if (glyph.PackId != ImFontAtlasRectId_Invalid) {
        ImFontAtlasPackDiscardRect(atlas, glyph.PackId);
      }
      continue;
    }
    if (i == baked->FallbackGlyphIndex) {
      fallback = kept.Size;
    }
    kept.push_back(glyph);
    top = std::max(top, static_cast<unsigned int>(glyph.Codepoint));
  }
  // Index sized to the static set; dynamic entries, "not found" marks included, are gone.
  const int size = static_cast<int>(top) + 1;
  ImVector<float> advance;
  ImVector<ImU16> lookup;
  advance.reserve(size);
  lookup.reserve(size);
  advance.resize(size, -1.0f);
  lookup.resize(size, kIndexUnused);
  const int carried = std::min(size, baked->IndexLookup.Size);
  for (int c = 0; c < carried; ++c) {
    if (baked->IndexLookup[c] == kIndexNotFound) {
      lookup[c] = kIndexNotFound;
      advance[c] = baked->IndexAdvanceX[c];
    }
  }
  for (int k = 0; k < kept.Size; ++k) {
    lookup[static_cast<int>(kept[k].Codepoint)] = static_cast<ImU16>(k);
    advance[static_cast<int>(kept[k].Codepoint)] = kept[k].AdvanceX;
  }
  baked->Glyphs.swap(kept);
  baked->IndexLookup.swap(lookup);
  baked->IndexAdvanceX.swap(advance);
  baked->FallbackGlyphIndex = fallback;
  ImFontAtlasTextureCompact(atlas);
  g_seen.resize(0);
  g_bake = baked;
  g_bakeId = baked->BakedId;
  trackNewGlyphs(baked);
}

// ImGui grows the per-codepoint index by half its size at a time; between frames, drop the spare capacity.
template <typename T>
void trimCapacity(ImVector<T> *vector) {
  if (vector->Capacity - vector->Size <= 256) {
    return;
  }
  ImVector<T> exact;
  exact.reserve(vector->Size);
  exact.resize(vector->Size);
  if (vector->Size > 0) {
    std::memcpy(exact.Data, vector->Data, static_cast<size_t>(vector->Size) * sizeof(T));
  }
  vector->swap(exact);
}

void publish(const ImFontBaked *baked, bool force) {
  long long index = 0;
  int dynamic = 0, visible = 0;
  if (baked != nullptr) {
    index = static_cast<long long>(baked->Glyphs.Capacity) * static_cast<long long>(sizeof(ImFontGlyph)) +
            static_cast<long long>(baked->IndexLookup.Capacity) * static_cast<long long>(sizeof(ImU16)) +
            static_cast<long long>(baked->IndexAdvanceX.Capacity) * static_cast<long long>(sizeof(float));
    for (int i = 0; i < g_seen.Size; ++i) {
      visible += (g_seen[i] >= 0 && g_boundary - g_seen[i] < g_limits.graceFrames) ? 1 : 0;
    }
  }
  g_indexBytes.store(index, std::memory_order_relaxed);
  g_dynamicVisible.store(visible, std::memory_order_relaxed);
  const int glyphs = baked != nullptr ? baked->Glyphs.Size : 0;
  if (!force && glyphs == g_snapshotGlyphs) {
    return;
  }
  g_snapshotGlyphs = glyphs;
  uint32_t codepoints[kSnapshotCap];
  int written = 0;
  for (int i = 0; i < glyphs; ++i) {
    const unsigned int c = baked->Glyphs[i].Codepoint;
    if (isDynamic(g_font, c)) {
      if (written < kSnapshotCap) {
        codepoints[written++] = c;
      }
      dynamic += 1;
    }
  }
  std::sort(codepoints, codepoints + written);
  g_dynamicGlyphs.store(dynamic, std::memory_order_relaxed);
  g_snapshotSeq.fetch_add(1, std::memory_order_acq_rel);
  for (int i = 0; i < written; ++i) {
    g_snapshot[i].store(codepoints[i], std::memory_order_relaxed);
  }
  g_snapshotCount.store(dynamic, std::memory_order_relaxed);
  g_snapshotSeq.fetch_add(1, std::memory_order_acq_rel);
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

bool admitDynamic(long long livePackedSurface, float fontSize, const Limits &limits) {
  // Reserve a glyph up to two ems wide plus padding.
  const long long side = static_cast<long long>(fontSize * 2.0f) + 2;
  const long long capacity = static_cast<long long>(limits.texMaxWidth) * limits.texMaxHeight;
  return livePackedSurface + side * side <= static_cast<long long>(static_cast<double>(capacity) * limits.fillLimit);
}

ImFont *setup(ImFontAtlas *atlas, const SharedFont *shared, int count, const Limits &limits) {
  g_limits = limits;
  g_font = nullptr;
  g_boundary = 0;
  g_lastRebuild = 0;
  g_refusedSinceRebuild = false;
  g_bake = nullptr;
  g_bakeId = 0;
  g_snapshotGlyphs = -1;
  g_seen.clear();
  g_sharedSources.store(0, std::memory_order_relaxed);
  g_rejectedSources.store(0, std::memory_order_relaxed);
  g_staticGlyphs.store(0, std::memory_order_relaxed);
  g_dynamicGlyphs.store(0, std::memory_order_relaxed);
  g_dynamicVisible.store(0, std::memory_order_relaxed);
  g_dynamicLoads.store(0, std::memory_order_relaxed);
  g_refusals.store(0, std::memory_order_relaxed);
  g_rebuilds.store(0, std::memory_order_relaxed);
  g_misplaced.store(0, std::memory_order_relaxed);
  g_snapshotCount.store(0, std::memory_order_relaxed);

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
    config.FontLoader = guardedLoader();
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
  g_font = font;

  // The static set, once: every static codepoint some source has, and the fallback and ellipsis glyphs.
  ImFontBaked *baked = font->GetFontBaked(kFontSize);
  if (baked != nullptr) {
    // Measuring text (CalcTextSize) loads advances only; a glyph is rasterized when a string draws it.
    baked->LoadNoRenderOnLayout = true;
    baked->FindGlyph(font->FallbackChar);
    for (unsigned int c = 0x20; c <= 0x17F; ++c) {
      if (isStaticCodepoint(c)) {
        baked->FindGlyph(static_cast<ImWchar>(c));
      }
    }
    baked->FindGlyph(font->EllipsisChar);
    g_staticGlyphs.store(baked->Glyphs.Size, std::memory_order_relaxed);
    trackNewGlyphs(baked);
    publish(baked, true);
  }
  return font;
}

bool frameBoundary(ImFontAtlas *atlas, const ImDrawData *drawData) {
  const ImGuiContext *context = ImGui::GetCurrentContext();
  if (context != nullptr && context->WithinFrameScope) {
    g_misplaced.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  g_boundary += 1;
  ImFontBaked *baked = mainBake(atlas);
  if (baked == nullptr) {
    publish(nullptr, false);
    return false;
  }
  trackNewGlyphs(baked);
  trimCapacity(&baked->IndexLookup);
  trimCapacity(&baked->IndexAdvanceX);
  int tracked = 0, left = 0, visible = 0;
  for (int i = 0; i < g_seen.Size; ++i) {
    tracked += g_seen[i] != kUntracked ? 1 : 0;
  }
  const int interval = g_limits.scanInterval > 0 ? g_limits.scanInterval : 1;
  if (tracked > 0 && drawData != nullptr && drawData->Valid && atlas->TexData != nullptr &&
      g_boundary % interval == 0) {
    scanDrawData(atlas, baked, drawData);
  }
  for (int i = 0; i < g_seen.Size; ++i) {
    if (g_seen[i] >= 0) {
      const bool recent = g_boundary - g_seen[i] < g_limits.graceFrames;
      visible += recent ? 1 : 0;
      left += recent ? 0 : 1;
    }
  }
  // Something that was on screen has left it; or the fill guard refused while glyphs not on screen hold space.
  const bool crowded = g_refusedSinceRebuild && tracked > visible &&
                       g_boundary - g_lastRebuild >= g_limits.refusalCooldownFrames;
  if (left == 0 && !crowded) {
    publish(baked, false);
    return false;
  }
  rebuild(atlas, baked);
  g_lastRebuild = g_boundary;
  g_refusedSinceRebuild = false;
  g_rebuilds.fetch_add(1, std::memory_order_relaxed);
  publish(baked, true);
  return true;
}

int dynamicSet(uint32_t *out, int cap) {
  for (int attempt = 0; attempt < 8; ++attempt) {
    const unsigned before = g_snapshotSeq.load(std::memory_order_acquire);
    if (before & 1u) {
      continue;
    }
    const int count = g_snapshotCount.load(std::memory_order_relaxed);
    const int copy = std::min(std::min(count, cap), kSnapshotCap);
    for (int i = 0; i < copy; ++i) {
      out[i] = g_snapshot[i].load(std::memory_order_relaxed);
    }
    if (g_snapshotSeq.load(std::memory_order_acquire) == before) {
      return count;
    }
  }
  return -1;
}

Stats stats() {
  return {g_sharedSources.load(std::memory_order_relaxed),  g_rejectedSources.load(std::memory_order_relaxed),
          g_fallbackChar.load(std::memory_order_relaxed),   g_staticGlyphs.load(std::memory_order_relaxed),
          g_dynamicGlyphs.load(std::memory_order_relaxed),  g_dynamicVisible.load(std::memory_order_relaxed),
          g_dynamicLoads.load(std::memory_order_relaxed),   g_refusals.load(std::memory_order_relaxed),
          g_rebuilds.load(std::memory_order_relaxed),       g_misplaced.load(std::memory_order_relaxed),
          g_indexBytes.load(std::memory_order_relaxed)};
}

}  // namespace ImguiFontSetup
