// Dear ImGui's paged per-codepoint index (patches/imgui/0001-paged-codepoint-index.patch) against the dense one.
//
// run.py builds this file twice, against the unpatched extern/imgui and against the patched copy, and runs it three
// times: unpatched (dense), patched with ImFontAtlasFlags_PagedCodepointIndex off (dense), patched with it on
// (paged). Each run writes what ImGui answers for a corpus, in two font configurations (fallback '?' and U+FFFD):
// - every codepoint's index entry after measuring, after drawing, after discarding glyphs and at the end;
// - each character's glyph (box, UVs, advance), FindGlyphNoFallback, IsGlyphLoaded and GetCharAdvance;
// - CalcTextSize, word-wrap positions, the vertices RenderText emits, and text measured under the password font;
// - the layout of ImFontBaked, ImFontAtlas, ImFont, ImFontGlyph and ImGuiContext.
// run.py requires the three dumps to be identical, apart from "mode" lines and "bytes" lines (the index's own
// memory: compared between the two dense runs only). The "bench" mode times CalcTextSize over the labels.
//
// Usage: paged_index_test <dense|paged|bench> <label-fixture.ttf> <JetBrainsMonoNL-Regular.ttf> <out.txt>
#include "imgui.h"
#include "imgui_internal.h"
#include "JetBrainsMonoNL-Regular.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

#ifdef IM_FONTBAKED_INDEX_PAGE_SHIFT
constexpr bool kPatched = true;
#else
constexpr bool kPatched = false;
#endif

FILE *g_out = nullptr;
int g_failures = 0;

void fail(const char *what) {
  std::fprintf(stderr, "FAIL %s\n", what);
  g_failures += 1;
}

unsigned bits(float f) {
  unsigned u = 0;
  std::memcpy(&u, &f, sizeof(u));
  return u;
}

std::vector<unsigned char> readFile(const char *path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string utf8Of(unsigned first, int count) {
  std::string out;
  for (int i = 0; i < count; ++i) {
    char buf[5] = {};
    ImTextCharToUtf8(buf, first + static_cast<unsigned>(i));
    out += buf;
  }
  return out;
}

std::vector<std::string> corpus() {
  return {
      // ASCII
      " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~",
      // Latin-1 Supplement
      "Æsir Ølstue Ærø ¡¿ßñüéèçàâÿ ×÷ µ©",
      // Latin Extended-A (Š ć Ł ź are not in the fixture)
      "Ōno Œuvre Āā Ăă Đđ Ēē Ěě Ĩĩ Īī Ńń Ňň Ŏŏ Ũũ Ūū Ŭŭ Šimić Łódź",
      // the kana label
      "Opp: Æsir テスト | 4 (12ms)",
      // kana and kanji (ひらがな カタカナ partly missing)
      "日本語のなまえ ひらがな カタカナ",
      // 48 kanji from U+4E00
      utf8Of(0x4E00, 48),
      // Hangul
      "한글 강철무사",
      // Cyrillic
      "Иван",
      // missing: Thai, Arabic, Devanagari, a private-use codepoint, U+FFFF
      "กขค عربي हिन्दी \xEE\x80\x80 \xEF\xBF\xBF",
      // astral codepoints: U+FFFD in this ImWchar16 build
      "😀 𝄞",
      // the ideographic space and a tab
      "\xE3\x80\x80tab\there",
  };
}

const char *const kLabels[][2] = {
    {"latin", "Opp: LLUltra | 4 (12ms)"},
    {"kana-label", "Opp: Æsir テスト | 4 (12ms)"},
    {"japanese", "Opp: 日本語のなまえ | 4 (12ms)"},
    {"korean", "Opp: 강철무사 | 4 (12ms)"},
    {"all-four", "LLUltra Æsir テスト 日本語のなまえ 강철무사 | 4 (12ms)"},
};

// What the bake's index holds for `c`, read the way this build's ImGui reads it.
std::pair<unsigned, float> entry(const ImFontBaked *baked, unsigned c) {
#ifdef IM_FONTBAKED_INDEX_PAGE_SHIFT
  return {baked->IndexGetLookup(c), baked->IndexGetAdvanceX(c)};
#else
  const unsigned lookup = c < static_cast<unsigned>(baked->IndexLookup.Size) ? baked->IndexLookup.Data[c] : 0xFFFFu;
  const float advance = c < static_cast<unsigned>(baked->IndexAdvanceX.Size) ? baked->IndexAdvanceX.Data[c] : -1.0f;
  return {lookup, advance};
#endif
}

void dumpIndex(const ImFontBaked *baked, const char *stage) {
  int entries = 0;
  for (unsigned c = 0; c <= IM_UNICODE_CODEPOINT_MAX; ++c) {
    const auto [lookup, advance] = entry(baked, c);
    if (lookup != 0xFFFFu || bits(advance) != bits(-1.0f)) {
      std::fprintf(g_out, "index %s U+%04X lookup=%u advance=%08x\n", stage, c, lookup, bits(advance));
      entries += 1;
    }
  }
  std::fprintf(g_out, "index %s entries=%d glyphs=%d\n", stage, entries, baked->Glyphs.Size);
  std::fprintf(g_out, "bytes %s lookup=%d/%d advance=%d/%d\n", stage, baked->IndexLookup.Size,
               baked->IndexLookup.Capacity, baked->IndexAdvanceX.Size, baked->IndexAdvanceX.Capacity);
#ifdef IM_FONTBAKED_INDEX_PAGE_SHIFT
  std::fprintf(g_out, "mode %s paged=%d pages=%d codepoint-index=%d B\n", stage, static_cast<int>(baked->IndexPaged),
               baked->IndexPaged ? baked->IndexAdvanceX.Size / IM_FONTBAKED_INDEX_PAGE_SIZE : 0,
               baked->IndexLookup.Capacity * 2 + baked->IndexAdvanceX.Capacity * 4);
#endif
}

void dumpGlyph(const char *what, unsigned c, const ImFontGlyph *g) {
  if (g == nullptr) {
    std::fprintf(g_out, "%s U+%04X null\n", what, c);
    return;
  }
  std::fprintf(g_out, "%s U+%04X cp=U+%04X visible=%u colored=%u src=%u adv=%08x box=%08x,%08x,%08x,%08x "
               "uv=%08x,%08x,%08x,%08x\n",
               what, c, static_cast<unsigned>(g->Codepoint), static_cast<unsigned>(g->Visible),
               static_cast<unsigned>(g->Colored), static_cast<unsigned>(g->SourceIdx), bits(g->AdvanceX),
               bits(g->X0), bits(g->Y0), bits(g->X1), bits(g->Y1), bits(g->U0), bits(g->V0), bits(g->U1),
               bits(g->V1));
}

void honourTextures() {
  static intptr_t next = 1;
  for (ImTextureData *tex : ImGui::GetPlatformIO().Textures) {
    if (tex->Status == ImTextureStatus_WantCreate) {
      tex->SetTexID(static_cast<ImTextureID>(next++));
      tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantUpdates) {
      tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames >= 0) {
      tex->SetTexID(ImTextureID_Invalid);
      tex->BackendUserData = nullptr;
      tex->SetStatus(ImTextureStatus_Destroyed);
    }
  }
}

void dumpDrawData(const char *stage) {
  const ImDrawData *data = ImGui::GetDrawData();
  int vertices = 0;
  for (const ImDrawList *list : data->CmdLists) {
    for (const ImDrawVert &v : list->VtxBuffer) {
      std::fprintf(g_out, "vertex %s %08x,%08x %08x,%08x %08x\n", stage, bits(v.pos.x), bits(v.pos.y), bits(v.uv.x),
                   bits(v.uv.y), v.col);
      vertices += 1;
    }
    for (const ImDrawCmd &cmd : list->CmdBuffer) {
      std::fprintf(g_out, "command %s elements=%u offset=%u texture=%d\n", stage, cmd.ElemCount, cmd.IdxOffset,
                   cmd.TexRef._TexData != nullptr ? cmd.TexRef._TexData->UniqueID : -1);
    }
  }
  std::fprintf(g_out, "vertex %s count=%d\n", stage, vertices);
}

// Where the set bits of a zeroed T are after `set` wrote a bitfield, as "first-last".
template <typename T, typename F>
std::string bitRange(F set) {
  T object;
  std::memset(static_cast<void *>(&object), 0, sizeof(T));
  set(&object);
  const unsigned char *raw = reinterpret_cast<const unsigned char *>(&object);
  int first = -1, last = -1;
  for (size_t i = 0; i < sizeof(T) * 8; ++i) {
    if (raw[i / 8] & (1u << (i % 8))) {
      first = first < 0 ? static_cast<int>(i) : first;
      last = static_cast<int>(i);
    }
  }
  return std::to_string(first) + "-" + std::to_string(last);
}

template <typename T, typename M>
size_t offsetIn(const T &object, const M &member) {
  return static_cast<size_t>(reinterpret_cast<const char *>(&member) - reinterpret_cast<const char *>(&object));
}

void dumpLayout() {
  static ImFontBaked baked;
  static ImFont font;
  static ImFontGlyph glyph;
  std::fprintf(g_out, "layout sizeof ImFontBaked=%zu ImFontAtlas=%zu ImFont=%zu ImFontGlyph=%zu ImGuiContext=%zu\n",
               sizeof(ImFontBaked), sizeof(ImFontAtlas), sizeof(ImFont), sizeof(ImFontGlyph), sizeof(ImGuiContext));
  std::fprintf(g_out,
               "layout ImFontBaked IndexAdvanceX=%zu FallbackAdvanceX=%zu Size=%zu RasterizerDensity=%zu "
               "IndexLookup=%zu Glyphs=%zu FallbackGlyphIndex=%zu Ascent=%zu Descent=%zu LastUsedFrame=%zu "
               "BakedId=%zu OwnerFont=%zu FontLoaderDatas=%zu\n",
               offsetIn(baked, baked.IndexAdvanceX), offsetIn(baked, baked.FallbackAdvanceX),
               offsetIn(baked, baked.Size), offsetIn(baked, baked.RasterizerDensity), offsetIn(baked, baked.IndexLookup),
               offsetIn(baked, baked.Glyphs), offsetIn(baked, baked.FallbackGlyphIndex), offsetIn(baked, baked.Ascent),
               offsetIn(baked, baked.Descent), offsetIn(baked, baked.LastUsedFrame), offsetIn(baked, baked.BakedId),
               offsetIn(baked, baked.OwnerFont), offsetIn(baked, baked.FontLoaderDatas));
  std::fprintf(g_out, "layout ImFontBaked bits MetricsTotalSurface=%s WantDestroy=%s LoadNoFallback=%s "
               "LoadNoRenderOnLayout=%s\n",
               bitRange<ImFontBaked>([](ImFontBaked *b) { b->MetricsTotalSurface = (1u << 26) - 1; }).c_str(),
               bitRange<ImFontBaked>([](ImFontBaked *b) { b->WantDestroy = 1; }).c_str(),
               bitRange<ImFontBaked>([](ImFontBaked *b) { b->LoadNoFallback = 1; }).c_str(),
               bitRange<ImFontBaked>([](ImFontBaked *b) { b->LoadNoRenderOnLayout = 1; }).c_str());
  std::fprintf(g_out, "layout ImFont LastBaked=%zu OwnerAtlas=%zu Flags=%zu FontId=%zu Sources=%zu "
               "EllipsisChar=%zu FallbackChar=%zu\n",
               offsetIn(font, font.LastBaked), offsetIn(font, font.OwnerAtlas), offsetIn(font, font.Flags),
               offsetIn(font, font.FontId), offsetIn(font, font.Sources), offsetIn(font, font.EllipsisChar),
               offsetIn(font, font.FallbackChar));
  std::fprintf(g_out, "layout ImFontGlyph AdvanceX=%zu X0=%zu U0=%zu PackId=%zu\n", offsetIn(glyph, glyph.AdvanceX),
               offsetIn(glyph, glyph.X0), offsetIn(glyph, glyph.U0), offsetIn(glyph, glyph.PackId));
  std::fprintf(g_out, "layout ImFontAtlasFlags NoPowerOfTwoHeight=%d NoMouseCursors=%d NoBakedLines=%d\n",
               static_cast<int>(ImFontAtlasFlags_NoPowerOfTwoHeight), static_cast<int>(ImFontAtlasFlags_NoMouseCursors),
               static_cast<int>(ImFontAtlasFlags_NoBakedLines));
#ifdef IM_FONTBAKED_INDEX_PAGE_SHIFT
  // The new bit sits beside the old ones without moving them (their positions are in the "layout" lines above).
  const std::string paged = bitRange<ImFontBaked>([](ImFontBaked *b) { b->IndexPaged = 1; });
  const std::set<std::string> old = {
      bitRange<ImFontBaked>([](ImFontBaked *b) { b->MetricsTotalSurface = (1u << 26) - 1; }),
      bitRange<ImFontBaked>([](ImFontBaked *b) { b->WantDestroy = 1; }),
      bitRange<ImFontBaked>([](ImFontBaked *b) { b->LoadNoFallback = 1; }),
      bitRange<ImFontBaked>([](ImFontBaked *b) { b->LoadNoRenderOnLayout = 1; })};
  const int bit = std::stoi(paged);
  const int lastOld = std::stoi(bitRange<ImFontBaked>([](ImFontBaked *b) { b->LoadNoRenderOnLayout = 1; }));
  std::fprintf(g_out, "mode layout IndexPaged bit=%s flag=%d\n", paged.c_str(),
               static_cast<int>(ImFontAtlasFlags_PagedCodepointIndex));
  if (old.count(paged) != 0 || bit != lastOld + 1 || bit / 32 != lastOld / 32) {
    fail("IndexPaged is the next spare bit of the same word");
  }
  if ((ImFontAtlasFlags_PagedCodepointIndex & (ImFontAtlasFlags_NoPowerOfTwoHeight | ImFontAtlasFlags_NoMouseCursors |
                                                ImFontAtlasFlags_NoBakedLines)) != 0) {
    fail("the atlas flag is a new bit");
  }
#endif
}

struct Config {
  const char *name;
  bool replacement;  // merge full JetBrains Mono too: its U+FFFD becomes the fallback
};

void frameBegin() {
  ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(1200, 700));
  ImGui::Begin("Corpus", nullptr, ImGuiWindowFlags_NoScrollbar);
}

void frameEnd() {
  ImGui::End();
  ImGui::Render();
  honourTextures();
}

// A context with the overlay's font configuration (font_setup.cpp), without its static set and drops: those are
// font_setup_test's. Leaves it current.
ImFont *makeContext(const Config &config, bool paged, const std::vector<unsigned char> &fixture,
                    const std::vector<unsigned char> &jetbrains) {
  // CreateContext() keeps the previous context current when there is one.
  ImGui::SetCurrentContext(ImGui::CreateContext());
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = ImVec2(1280, 720);
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
  ImFontAtlas *atlas = io.Fonts;
#ifdef IM_FONTBAKED_INDEX_PAGE_SHIFT
  if (paged) {
    atlas->Flags |= ImFontAtlasFlags_PagedCodepointIndex;
  }
#else
  if (paged) {
    fail("the unpatched build has no paged index");
  }
#endif
  atlas->TexDesiredFormat = ImTextureFormat_Alpha8;
  atlas->TexMinWidth = 512;
  atlas->TexMinHeight = 128;
  atlas->TexMaxWidth = 1024;
  atlas->TexMaxHeight = 1024;
  ImFont *font = atlas->AddFontFromMemoryCompressedTTF(JetBrainsMonoNL_compressed_data, JetBrainsMonoNL_compressed_size,
                                                       18.0f);
  font->Flags |= ImFontFlags_LockBakedSizes;
  std::vector<const std::vector<unsigned char> *> sources = {&fixture};
  if (config.replacement) {
    sources.push_back(&jetbrains);
  }
  for (const std::vector<unsigned char> *data : sources) {
    ImFontConfig source;
    source.FontData = const_cast<unsigned char *>(data->data());
    source.FontDataSize = static_cast<int>(data->size());
    source.FontDataOwnedByAtlas = false;
    source.MergeMode = true;
    source.OversampleH = 1;
    source.OversampleV = 1;
    atlas->AddFont(&source);
  }
  if (font->IsGlyphInFont(IM_UNICODE_CODEPOINT_INVALID)) {
    font->FallbackChar = IM_UNICODE_CODEPOINT_INVALID;
  }
  io.FontDefault = font;
  font->GetFontBaked(18.0f)->LoadNoRenderOnLayout = true;
  return font;
}

void runConfig(const Config &config, bool paged, const std::vector<unsigned char> &fixture,
               const std::vector<unsigned char> &jetbrains) {
  std::fprintf(g_out, "config %s\n", config.name);
  ImFont *font = makeContext(config, paged, fixture, jetbrains);
  ImGuiContext *context = ImGui::GetCurrentContext();
  ImFontAtlas *atlas = font->OwnerAtlas;
  ImFontBaked *baked = font->GetFontBaked(18.0f);
  std::fprintf(g_out, "fallback U+%04X ellipsis U+%04X\n", static_cast<unsigned>(font->FallbackChar),
               static_cast<unsigned>(font->EllipsisChar));
  dumpIndex(baked, "created");

  const std::vector<std::string> texts = corpus();
  std::set<unsigned> codepoints = {0x09, 0x20, 0x41, 0x7F, 0xA0, 0xFF, 0x100, 0x17F, 0x180, 0x2026, 0x3000, 0x30C6,
                                   0x4E00, 0x4EFF, 0xAC00, 0xD7A3, 0xE000, 0xFFFD, 0xFFFE, 0xFFFF};
  for (const std::string &text : texts) {
    const char *s = text.c_str();
    const char *end = s + text.size();
    while (s < end) {
      unsigned c = 0;
      s += ImTextCharFromUtf8(&c, s, end);
      codepoints.insert(c);
    }
  }

  // Measuring loads advances only (LoadNoRenderOnLayout, as the overlay sets it).
  frameBegin();
  for (size_t i = 0; i < texts.size(); ++i) {
    const ImVec2 size = ImGui::CalcTextSize(texts[i].c_str());
    std::fprintf(g_out, "measured %zu %08x,%08x\n", i, bits(size.x), bits(size.y));
  }
  frameEnd();
  dumpIndex(baked, "measured");

  // Drawing loads the glyphs: plain lines, then wrapped at 200 px (RenderText's word wrap).
  frameBegin();
  for (const std::string &text : texts) {
    ImGui::TextUnformatted(text.c_str());
  }
  ImGui::PushTextWrapPos(200.0f);
  for (const std::string &text : texts) {
    ImGui::TextUnformatted(text.c_str());
  }
  ImGui::PopTextWrapPos();
  frameEnd();
  dumpDrawData("drawn");
  dumpIndex(baked, "drawn");

  for (unsigned c : codepoints) {
    dumpGlyph("glyph", c, baked->FindGlyph(static_cast<ImWchar>(c)));
    dumpGlyph("nofallback", c, baked->FindGlyphNoFallback(static_cast<ImWchar>(c)));
    const ImWchar w = static_cast<ImWchar>(c);
    std::fprintf(g_out, "loaded U+%04X %d advance=%08x\n", c, static_cast<int>(baked->IsGlyphLoaded(w)),
                 bits(baked->GetCharAdvance(w)));
  }
  for (size_t i = 0; i < texts.size(); ++i) {
    const char *s = texts[i].c_str();
    const char *end = s + texts[i].size();
    for (float width : {30.0f, 60.0f, 120.0f, 240.0f}) {
      const char *wrap = font->CalcWordWrapPosition(18.0f, s, end, width);
      const ImVec2 size = font->CalcTextSizeA(18.0f, width, width, s, end);
      std::fprintf(g_out, "wrap %zu %d at=%td size=%08x,%08x\n", i, static_cast<int>(width), wrap - s, bits(size.x),
                   bits(size.y));
    }
    const ImVec2 scaled = font->CalcTextSizeA(13.5f, FLT_MAX, 0.0f, s, end);
    std::fprintf(g_out, "scaled %zu %08x,%08x\n", i, bits(scaled.x), bits(scaled.y));
  }

  // Discarded glyphs: the index forgets them, and the next lookup loads them again.
  for (unsigned c : {0x30C6u, 0xAC15u, 0x014Cu, 0x4E01u}) {
    if (baked->IsGlyphLoaded(static_cast<ImWchar>(c))) {
      ImFontAtlasBakedDiscardFontGlyph(atlas, font, baked, baked->FindGlyph(static_cast<ImWchar>(c)));
    }
    const auto [lookup, advance] = entry(baked, c);
    std::fprintf(g_out, "discarded U+%04X lookup=%u advance=%08x loaded=%d\n", c, lookup, bits(advance),
                 static_cast<int>(baked->IsGlyphLoaded(static_cast<ImWchar>(c))));
    dumpGlyph("reloaded", c, baked->FindGlyph(static_cast<ImWchar>(c)));
  }
  dumpIndex(baked, "discarded");

  // The password font swaps the index out for a frame's text (imgui_widgets.cpp PushPasswordFont).
  frameBegin();
  ImGui::PushPasswordFont();
  for (const char *text : {"pass 日本語 강", "Æsir テスト"}) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    std::fprintf(g_out, "password %08x,%08x\n", bits(size.x), bits(size.y));
  }
  dumpGlyph("password", 'x', ImGui::GetFontBaked()->FindGlyph('x'));
  ImGui::PopPasswordFont();
  ImGui::TextUnformatted("after the password font: Æsir テスト");
  frameEnd();
  dumpDrawData("password");
  dumpIndex(baked, "final");
  ImGui::DestroyContext(context);
}

// CalcTextSize over each label once its glyphs are loaded. The patched build times its dense and paged contexts in
// alternating rounds, so both see the same machine load; each figure is the median of the rounds.
void bench(const std::vector<unsigned char> &fixture, const std::vector<unsigned char> &jetbrains) {
  struct Variant {
    const char *name;
    ImGuiContext *context;
    ImFont *font;
    std::vector<double> rounds;
  };
  std::vector<Variant> variants;
  for (const bool paged : {false, true}) {
    if (paged && !kPatched) {
      continue;
    }
    ImFont *font = makeContext({"bench", false}, paged, fixture, jetbrains);
    const char *name = !kPatched ? "upstream-dense" : (paged ? "patched-paged" : "patched-dense");
    variants.push_back({name, ImGui::GetCurrentContext(), font, {}});
    frameBegin();
    for (const auto &label : kLabels) {
      ImGui::TextUnformatted(label[1]);
    }
    frameEnd();
  }
  constexpr int kIterations = 50000;
  constexpr int kRounds = 31;
  for (const auto &label : kLabels) {
    const char *end = label[1] + std::strlen(label[1]);
    for (Variant &v : variants) {
      v.rounds.clear();
    }
    volatile float sink = 0.0f;
    for (int round = 0; round < kRounds; ++round) {
      for (Variant &v : variants) {
        ImGui::SetCurrentContext(v.context);
        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < kIterations; ++i) {
          sink = sink + v.font->CalcTextSizeA(18.0f, FLT_MAX, 0.0f, label[1], end).x;
        }
        const auto took = std::chrono::steady_clock::now() - begin;
        v.rounds.push_back(std::chrono::duration<double, std::nano>(took).count() / kIterations);
      }
    }
    for (Variant &v : variants) {
      std::sort(v.rounds.begin(), v.rounds.end());
      std::fprintf(g_out, "bench %s %s %.2f\n", label[0], v.name, v.rounds[v.rounds.size() / 2]);
    }
  }
  for (Variant &v : variants) {
    ImGui::DestroyContext(v.context);
  }
}

}  // namespace

int main(int argc, char **argv) {
  const std::string mode = argc >= 2 ? argv[1] : "";
  if (argc < 5 || (mode != "dense" && mode != "paged" && mode != "bench")) {
    std::fprintf(stderr, "usage: paged_index_test <dense|paged|bench> <label-fixture.ttf> "
                         "<JetBrainsMonoNL-Regular.ttf> <out.txt>\n");
    return 2;
  }
  const std::vector<unsigned char> fixture = readFile(argv[2]);
  const std::vector<unsigned char> jetbrains = readFile(argv[3]);
  g_out = std::fopen(argv[4], "w");
  if (fixture.empty() || jetbrains.empty() || g_out == nullptr) {
    std::fprintf(stderr, "inputs not readable or output not writable\n");
    return 2;
  }
  std::fprintf(g_out, "mode build=%s index=%s\n", kPatched ? "patched" : "upstream", mode.c_str());
  if (mode == "bench") {
    bench(fixture, jetbrains);
  } else {
    dumpLayout();
    runConfig({"fallback '?'", false}, mode == "paged", fixture, jetbrains);
    runConfig({"fallback U+FFFD", true}, mode == "paged", fixture, jetbrains);
  }
  std::fclose(g_out);
  return g_failures == 0 ? 0 : 1;
}
