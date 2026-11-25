// Host tests of the overlay's fonts (src/imgui_backend/font_setup.cpp) through ImGui's own text path:
// ImGui::TextUnformatted in a window, as Deluxe draws its labels, in the host's frame loop (procDraw):
// frameBoundary() with the last frame's draw data, NewFrame, the windows, Render, then the backend's side of
// ImGuiBackendFlags_RendererHasTextures. A drawn character's glyph is what ImFont::RenderText draws for it:
// baked->FindGlyph() of the codepoint ImTextCharFromUtf8 decodes.
//
// Usage: font_setup_test <host-tests/fixtures/label-fixture.ttf>
#include "imgui_backend/font_feed.hpp"
#include "imgui_backend/font_setup.hpp"
#include "imgui_backend/font_retry.hpp"
#include "font_fixture.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using ImguiFontSetup::Limits;
using ImguiFontSetup::SharedFont;

int g_failures = 0;
// Every test runs twice: with ImGui's dense per-codepoint index, then with the paged one (usePagedIndex).
bool g_paged = false;

void expect(bool condition, const char *name) {
  if (!condition) {
    std::fprintf(stderr, "FAIL [%s] %s\n", g_paged ? "paged" : "dense", name);
    g_failures += 1;
  }
}

// Per mode (0 dense, 1 paged), from test_index_bytes_by_language: stats().indexBytes (glyph table and codepoint
// index), the codepoint index alone and its pages, for Latin text, the kana label, and Hangul and kanji together.
constexpr int kScreens = 3;
const char *const kScreenNames[kScreens] = {"Latin only", "the kana label", "Hangul and kanji together"};
long long g_bytes[2][kScreens] = {};
long long g_codepointBytes[2][kScreens] = {};
int g_pages[2][kScreens] = {};

// Per mode, from test_frame_boundary_rule: what the bake's index answers for every codepoint, and its glyph table,
// after 400 frames of name changes and their rebuilds.
std::vector<std::pair<ImU16, float>> g_logical[2];
std::vector<float> g_glyphTable[2];

std::vector<std::pair<ImU16, float>> logicalIndex(const ImFontBaked *baked) {
  std::vector<std::pair<ImU16, float>> out;
  for (unsigned c = 0; c <= IM_UNICODE_CODEPOINT_MAX; ++c) {
    out.emplace_back(baked->IndexGetLookup(c), baked->IndexGetAdvanceX(c));
  }
  return out;
}

std::vector<float> glyphTable(const ImFontBaked *baked) {
  std::vector<float> out;
  for (const ImFontGlyph &g : baked->Glyphs) {
    out.insert(out.end(), {static_cast<float>(g.Codepoint), static_cast<float>(g.Visible), g.AdvanceX, g.X0, g.Y0,
                           g.X1, g.Y1, g.U0, g.V0, g.U1, g.V1});
  }
  return out;
}

// The paged index's page map: IndexLookup's first IM_FONTBAKED_INDEX_PAGE_COUNT entries.
bool pageAllocated(const ImFontBaked *baked, unsigned page) {
  return baked->IndexPaged && baked->IndexLookup.Size >= IM_FONTBAKED_INDEX_PAGE_COUNT &&
         baked->IndexLookup[static_cast<int>(page)] != 0;
}

const char *const kLabel = "Æsir テスト";
// Short limits so the tests run in few frames: a glyph has left the screen after 6 frames without a draw.
constexpr Limits kTest = {0.5f, 512, 128, 1024, 1024, 6, 1, 30};

std::vector<unsigned char> readFile(const char *path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

SharedFont shared(const std::vector<unsigned char> &bytes) { return {bytes.data(), bytes.size()}; }

struct Overlay {
  ImGuiContext *context;
  ImFont *font;

  explicit Overlay(std::vector<SharedFont> fonts, const Limits &limits = kTest) {
    context = ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    // InitBackend sets the flags before the fonts are added.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    font = ImguiFontSetup::setup(io.Fonts, fonts.data(), static_cast<int>(fonts.size()), limits);
  }
  ~Overlay() { ImGui::DestroyContext(context); }
};

// The backend's side of the texture protocol, as renderDrawData's updateTexture honours it. A texture asked to be
// destroyed is kept `destroyDelay` more frames, as the NVN backend keeps it while a submission may still read it.
int g_destroyDelay = 0;
std::set<int> g_destroyed;

void honourTextures() {
  static intptr_t next = 1;
  for (ImTextureData *tex : ImGui::GetPlatformIO().Textures) {
    if (tex->Status == ImTextureStatus_WantCreate) {
      tex->SetTexID(static_cast<ImTextureID>(next++));
      tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantUpdates) {
      tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames >= g_destroyDelay) {
      g_destroyed.insert(tex->UniqueID);
      tex->SetTexID(ImTextureID_Invalid);
      tex->BackendUserData = nullptr;
      tex->SetStatus(ImTextureStatus_Destroyed);
    }
  }
}

// Every font-atlas draw command of the frame names a texture the backend holds (created, not destroyed).
bool drawDataTexturesLive() {
  const ImDrawData *data = ImGui::GetDrawData();
  if (data == nullptr) {
    return true;
  }
  for (const ImDrawList *list : data->CmdLists) {
    for (const ImDrawCmd &cmd : list->CmdBuffer) {
      const ImTextureData *tex = cmd.TexRef._TexData;
      if (tex != nullptr && (g_destroyed.count(tex->UniqueID) != 0 || tex->Status != ImTextureStatus_OK ||
                             tex->TexID == ImTextureID_Invalid)) {
        return false;
      }
    }
  }
  return true;
}

struct FrameNote {
  bool rebuiltAtBoundary = false;
  bool changedInsideFrame = false;  // rebuild count or atlas texture changed between NewFrame and Render
  bool texturesLive = true;
  bool onCurrentAtlas = true;       // every font-atlas command names the atlas's current texture
};

bool drawDataOnAtlas(const ImTextureData *atlasTex) {
  const ImDrawData *data = ImGui::GetDrawData();
  for (const ImDrawList *list : data->CmdLists) {
    for (const ImDrawCmd &cmd : list->CmdBuffer) {
      if (cmd.ElemCount > 0 && cmd.TexRef._TexData != nullptr && cmd.TexRef._TexData != atlasTex) {
        return false;
      }
    }
  }
  return true;
}

// One frame of the host loop. `width` narrows the window to clip text; `measureOnly` lines are measured, not drawn.
FrameNote frame(const std::vector<std::string> &lines, float pushedSize = 0.0f, float width = 1200.0f,
                const std::vector<std::string> &measureOnly = {}) {
  FrameNote note;
  ImGuiIO &io = ImGui::GetIO();
  note.rebuiltAtBoundary = ImguiFontSetup::frameBoundary(io.Fonts, ImGui::GetDrawData());
  io.DeltaTime = 1.0f / 60.0f;
  ImGui::NewFrame();
  const long long rebuilds = ImguiFontSetup::stats().rebuilds;
  const ImTextureData *tex = io.Fonts->TexData;
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(width, 700));
  ImGui::Begin("Opp", nullptr, ImGuiWindowFlags_NoScrollbar);
  if (pushedSize > 0.0f) {
    ImGui::PushFont(nullptr, pushedSize);
  }
  for (const std::string &line : lines) {
    ImGui::TextUnformatted(line.c_str());
  }
  for (const std::string &line : measureOnly) {
    (void)ImGui::CalcTextSize(line.c_str());
  }
  if (pushedSize > 0.0f) {
    ImGui::PopFont();
  }
  ImGui::End();
  ImGui::Render();
  note.changedInsideFrame = ImguiFontSetup::stats().rebuilds != rebuilds || io.Fonts->TexData != tex;
  note.onCurrentAtlas = drawDataOnAtlas(io.Fonts->TexData);
  honourTextures();
  note.texturesLive = drawDataTexturesLive();
  return note;
}

ImFontBaked *bake(ImFont *font) { return font->GetFontBaked(ImguiFontSetup::kFontSize); }

// (codepoint decoded, codepoint of the glyph drawn for it) for each character of `utf8`.
std::vector<std::pair<unsigned, unsigned>> drawn(ImFont *font, const char *utf8) {
  ImFontBaked *baked = bake(font);
  std::vector<std::pair<unsigned, unsigned>> out;
  const char *s = utf8;
  const char *end = utf8 + std::strlen(utf8);
  while (s < end) {
    unsigned int c = 0;
    s += ImTextCharFromUtf8(&c, s, end);
    const ImFontGlyph *glyph = baked->FindGlyph(static_cast<ImWchar>(c));
    out.emplace_back(c, glyph != nullptr ? glyph->Codepoint : 0u);
  }
  return out;
}

bool allOwn(ImFont *font, const char *utf8) {
  for (auto [c, glyph] : drawn(font, utf8)) {
    if (c != glyph) {
      std::fprintf(stderr, "  U+%04X draws U+%04X\n", c, glyph);
      return false;
    }
  }
  return true;
}

// The codepoints of `utf8` outside the static set (the dynamic glyphs that text needs).
std::set<unsigned> dynamicOf(const std::string &utf8) {
  std::set<unsigned> out;
  const char *s = utf8.c_str();
  const char *end = s + utf8.size();
  while (s < end) {
    unsigned int c = 0;
    s += ImTextCharFromUtf8(&c, s, end);
    if (!ImguiFontSetup::isStaticCodepoint(c)) {
      out.insert(c);
    }
  }
  return out;
}

std::set<unsigned> dynamicSet() {
  uint32_t codepoints[256];
  const int count = ImguiFontSetup::dynamicSet(codepoints, 256);
  return std::set<unsigned>(codepoints, codepoints + std::max(0, std::min(count, 256)));
}

std::set<unsigned> unite(std::set<unsigned> a, const std::set<unsigned> &b) {
  a.insert(b.begin(), b.end());
  return a;
}

std::string utf8Of(const std::vector<unsigned> &codepoints) {
  std::string out;
  for (unsigned c : codepoints) {
    char buf[5] = {};
    ImTextCharToUtf8(buf, c);
    out += buf;
  }
  return out;
}

std::string kanji(int first, int count) {
  std::vector<unsigned> codepoints;
  for (int i = 0; i < count; ++i) {
    codepoints.push_back(static_cast<unsigned>(0x4E00 + first + i));
  }
  return utf8Of(codepoints);
}

long long livePacked() {
  const ImFontAtlasBuilder *builder = ImGui::GetIO().Fonts->Builder;
  return static_cast<long long>(builder->RectsPackedSurface) - builder->RectsDiscardedSurface;
}

int texturePixelBytes() {
  int bytes = 0;
  for (const ImTextureData *tex : ImGui::GetIO().Fonts->TexList) {
    bytes += tex->Pixels != nullptr ? tex->GetSizeInBytes() : 0;
  }
  return bytes;
}

unsigned read16(const unsigned char *p) { return (static_cast<unsigned>(p[0]) << 8) | p[1]; }
unsigned read32(const unsigned char *p) { return (read16(p) << 16) | read16(p + 2); }

size_t tableOffset(const std::vector<unsigned char> &font, const char *name) {
  if (font.size() < 12) {
    return 0;
  }
  const unsigned count = read16(font.data() + 4);
  for (unsigned i = 0; i < count && 12 + (i + 1) * 16 <= font.size(); ++i) {
    const auto *record = font.data() + 12 + i * 16;
    if (std::memcmp(record, name, 4) == 0) {
      const size_t offset = read32(record + 8);
      const size_t length = read32(record + 12);
      return offset <= font.size() && length <= font.size() - offset ? offset : 0;
    }
  }
  return 0;
}

void write16(std::vector<unsigned char> &font, size_t offset, int value) {
  font[offset] = static_cast<unsigned char>((value >> 8) & 0xff);
  font[offset + 1] = static_cast<unsigned char>(value & 0xff);
}

std::vector<unsigned char> lineVariant(const std::vector<unsigned char> &fixture, int ascent, int descent,
                                       bool cap = true) {
  std::vector<unsigned char> font = fixture;
  const size_t hhea = tableOffset(font, "hhea");
  const size_t os2 = tableOffset(font, "OS/2");
  if (hhea == 0 || os2 == 0 || hhea + 8 > font.size() || os2 + 90 > font.size()) {
    expect(false, "fixture has hhea and OS/2 metrics");
    return {};
  }
  write16(font, hhea + 4, ascent);
  write16(font, hhea + 6, descent);
  if (!cap) {
    write16(font, os2, 1);
  }
  expect(ImguiFontSetup::validFont(font.data(), font.size()), "each line-box variant remains valid");
  return font;
}

void test_the_shared_fonts_are_the_only_sources(const std::vector<unsigned char> &fixture) {
  Overlay overlay({shared(fixture)});
  expect(overlay.font != nullptr, "shared-only setup creates a font");
  if (overlay.font == nullptr) {
    return;
  }
  const ImFontGlyph *ascii = bake(overlay.font)->FindGlyph('H');
  const ImFontGlyph *accent = bake(overlay.font)->FindGlyph(0x00C6);
  std::printf("shared only: sources=%d shared=%d H-source=%d Æ-source=%d\n", overlay.font->Sources.Size,
              ImguiFontSetup::stats().sharedSources, ascii ? ascii->SourceIdx : -1, accent ? accent->SourceIdx : -1);
  expect(overlay.font->Sources.Size == 1 && ImguiFontSetup::stats().sharedSources == 1,
         "test_the_shared_fonts_are_the_only_sources: exactly one shared source");
  expect(overlay.font->Sources[0]->FontData == fixture.data() && ascii && ascii->Codepoint == 'H' &&
             ascii->SourceIdx == 0 && accent && accent->Codepoint == 0x00C6 && accent->SourceIdx == 0,
         "test_the_shared_fonts_are_the_only_sources: ASCII and Æ come from the fixture");
}

void test_no_valid_shared_font_adds_no_font() {
  std::vector<unsigned char> invalid(64, 0);
  for (const std::vector<SharedFont> &fonts : {std::vector<SharedFont>{}, std::vector<SharedFont>{shared(invalid)}}) {
    Overlay overlay(fonts);
    const int count = ImGui::GetIO().Fonts->Fonts.Size;
    std::printf("no valid shared font: inputs=%zu font=%s atlas-fonts=%d shared=%d\n", fonts.size(),
                overlay.font ? "present" : "null", count, ImguiFontSetup::stats().sharedSources);
    expect(overlay.font == nullptr && count == 0 && ImguiFontSetup::stats().sharedSources == 0,
           "test_no_valid_shared_font_adds_no_font: no font is added");
  }
}

std::vector<unsigned char> symbolFixture(const std::vector<unsigned char> &fixture, unsigned codepoint = 0xe000) {
  // Keep the fixture's outlines and metrics; give it a single glyph, a private-use icon by default.
  auto symbols = fixture;
  const size_t cmap = tableOffset(symbols, "cmap");
  if (cmap == 0 || cmap + 44 > symbols.size()) {
    expect(false, "fixture has space for a two-segment Unicode cmap");
    return {};
  }
  const unsigned words[] = {0, 1, 3, 1, 0, 12, 4, 32, 0, 4, 4, 1, 0, codepoint, 0xffff, 0,
                            codepoint, 0xffff, (2u - codepoint) & 0xffffu, 1, 0, 0};
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
    write16(symbols, cmap + 2 * i, words[i]);
  }
  expect(ImguiFontSetup::validFont(symbols.data(), symbols.size()), "the supplementary face is structurally valid");
  return symbols;
}

void test_symbol_only_does_not_make_the_overlay_ready(const std::vector<unsigned char> &fixture) {
  const auto symbols = symbolFixture(fixture);
  Overlay overlay({shared(symbols)});
  auto *atlas = ImGui::GetIO().Fonts;
  expect(!overlay.font && atlas->Fonts.Size == 0 && ImguiFontSetup::stats().sharedSources == 0,
         "a loaded symbol-only face cannot make the overlay ready");
  if (overlay.font) {
    return;
  }
  const SharedFont both[] = {shared(fixture), shared(symbols)};
  auto *ready = ImguiFontSetup::setup(atlas, both, 2);
  expect(ready && atlas->Fonts.Size == 1 && ready->IsGlyphInFont('H') && ready->IsGlyphInFont(0x30c6) &&
             ready->IsGlyphInFont(0xe000) && ImguiFontSetup::stats().sharedSources == 2,
         "same-context retry admits the text base and keeps its supplementary icon");
}

void test_late_shared_face_joins_the_live_font_once(const std::vector<unsigned char> &fixture) {
  g_destroyed.clear(); // Texture IDs start over with the new context.
  const auto symbols = symbolFixture(fixture);
  Overlay overlay({shared(fixture)});
  auto *atlas = ImGui::GetIO().Fonts;
  expect(overlay.font && !overlay.font->IsGlyphInFont(0xe000), "base alone has no icon");
  if (!overlay.font) {
    return;
  }
  constexpr char icon[] = "\xee\x80\x80"; // U+E000
  frame({icon});
  const SharedFont both[] = {shared(fixture), shared(symbols)};
  auto *font = ImguiFontSetup::setup(atlas, both, 2, kTest);
  expect(font == overlay.font && atlas->Fonts.Size == 1 && font->IsGlyphInFont(0xe000),
         "a later shared face extends the same live font");
  if (font != overlay.font) {
    return;
  }
  const auto shown = frame({icon});
  const auto *glyph = bake(font)->FindGlyph(0xe000);
  expect(shown.texturesLive && shown.onCurrentAtlas && glyph && glyph->Codepoint == 0xe000,
         "a previously missing icon draws from the current atlas after the merge");
  auto *same = ImguiFontSetup::setup(atlas, both, 2, kTest);
  expect(same == font && font->Sources.Size == 2 && ImguiFontSetup::stats().sharedSources == 2,
         "an unchanged provider set adds nothing twice");
}

// The font is a function of which faces are loaded, not of the order they arrived in: its sources stay in query
// order, with the first face that covers ordinary text as the base.
void test_the_base_is_chosen_by_font_order_not_arrival(const std::vector<unsigned char> &fixture) {
  g_destroyed.clear(); // Texture IDs start over with the new context.
  const auto regional = lineVariant(fixture, 880, -120);
  [&] {
    Overlay overlay({shared(regional)});
    auto *atlas = ImGui::GetIO().Fonts;
    expect(overlay.font && overlay.font->Sources[0]->FontData == regional.data(), "a regional text face alone is the base");
    if (!overlay.font) {
      return;
    }
    frame({"H"});
    // The standard face is first in query order and loads one poll later.
    const SharedFont both[] = {shared(fixture), shared(regional)};
    auto *font = ImguiFontSetup::setup(atlas, both, 2, kTest);
    const bool standardBase = font && atlas->Fonts.Size == 1 && font->Sources.Size == 2 &&
                              font->Sources[0]->FontData == fixture.data() && font->Sources[1]->FontData == regional.data();
    expect(standardBase, "the face first in query order becomes the base when it loads later");
    if (!standardBase) {
      return;
    }
    const auto shown = frame({"H"});
    const auto *h = bake(font)->FindGlyph('H');
    expect(shown.texturesLive && shown.onCurrentAtlas && h && h->Codepoint == 'H' && h->SourceIdx == 0,
           "Latin text is drawn from the base that is first in query order");
    expect(ImGui::GetFont() == font && ImguiFontSetup::stats().sharedSources == 2, "the context draws with the one font");
    auto *same = ImguiFontSetup::setup(atlas, both, 2, kTest);
    expect(same == font && atlas->Fonts.Size == 1 && font->Sources.Size == 2, "an unchanged set changes nothing");
  }();
  // Merged faces keep query order too: the second face arrives after the third.
  const auto second = symbolFixture(fixture, 0xe000), third = symbolFixture(fixture, 0xe001);
  Overlay overlay({shared(fixture), shared(third)});
  auto *atlas = ImGui::GetIO().Fonts;
  if (!overlay.font) {
    expect(false, "base with a later face exists");
    return;
  }
  frame({"H"});
  const SharedFont all[] = {shared(fixture), shared(second), shared(third)};
  auto *font = ImguiFontSetup::setup(atlas, all, 3, kTest);
  expect(font && atlas->Fonts.Size == 1 && font->Sources.Size == 3 && font->Sources[0]->FontData == fixture.data() &&
             font->Sources[1]->FontData == second.data() && font->Sources[2]->FontData == third.data(),
         "merged faces stay in query order when one arrives late");
  if (font) {
    const auto shown = frame({"H\xee\x80\x80\xee\x80\x81"});
    expect(shown.texturesLive && shown.onCurrentAtlas && font->IsGlyphInFont(0xe000) && font->IsGlyphInFont(0xe001),
           "both late faces draw from the current atlas");
  }
}

// A face that loads late can supply a static-set character the base lacks, while no dynamic glyph is loaded.
void test_a_late_face_adding_a_static_glyph_draws_without_a_rebuild(const std::vector<unsigned char> &fixture) {
  g_destroyed.clear();
  Overlay overlay({shared(fixture)});
  auto *atlas = ImGui::GetIO().Fonts;
  if (!overlay.font) {
    expect(false, "base exists");
    return;
  }
  unsigned missing = 0;
  for (unsigned c = 0xa0; c <= 0x17f && missing == 0; ++c) {
    if (!overlay.font->IsGlyphInFont(static_cast<ImWchar>(c))) {
      missing = c;
    }
  }
  expect(missing != 0, "the fixture lacks a static-set character");
  if (missing == 0) {
    return;
  }
  char utf8[3] = {static_cast<char>(0xc0 | (missing >> 6)), static_cast<char>(0x80 | (missing & 0x3f)), 0};
  frame({std::string("H") + utf8});
  const auto before = ImguiFontSetup::stats();
  const auto *fallback = bake(overlay.font)->FindGlyph(static_cast<ImWchar>(missing));
  expect(before.dynamicGlyphs == 0 && fallback && fallback->Codepoint != missing,
         "before the late face: no dynamic glyph, and the character draws the fallback");
  const auto late = symbolFixture(fixture, missing);
  const SharedFont both[] = {shared(fixture), shared(late)};
  auto *font = ImguiFontSetup::setup(atlas, both, 2, kTest);
  expect(font == overlay.font && font->Sources.Size == 2, "the late face joins the live font");
  FrameNote shown;
  for (int i = 0; i < 40; ++i) {
    shown = frame({std::string("H") + utf8});
  }
  const auto after = ImguiFontSetup::stats();
  const auto *glyph = bake(font)->FindGlyph(static_cast<ImWchar>(missing));
  std::printf("late static glyph U+%04X: static %d -> %d, dynamic %d, rebuilds %lld -> %lld, source %d\n", missing,
              before.staticGlyphs, after.staticGlyphs, after.dynamicGlyphs, before.rebuilds, after.rebuilds,
              glyph ? static_cast<int>(glyph->SourceIdx) : -1);
  expect(glyph && glyph->Codepoint == missing && glyph->SourceIdx == 1 && shown.texturesLive && shown.onCurrentAtlas,
         "the late static character draws its own glyph from the current atlas");
  expect(after.staticGlyphs == before.staticGlyphs + 1 && after.dynamicGlyphs == 0 && after.rebuilds == before.rebuilds,
         "it joins the static set: no dynamic glyph and no rebuild, over 40 frames");
}

// The same, with a dynamic glyph on screen: the glyph table is built again, so its tracking must start again too.
void test_a_late_face_keeps_the_dynamic_tracking_aligned(const std::vector<unsigned char> &fixture) {
  g_destroyed.clear();
  Overlay overlay({shared(fixture)});
  auto *atlas = ImGui::GetIO().Fonts;
  if (!overlay.font) {
    expect(false, "base exists");
    return;
  }
  const std::string line = "H \xe3\x83\x86"; // one dynamic glyph, U+30C6
  for (int i = 0; i < 3; ++i) {
    frame({line});
  }
  expect(ImguiFontSetup::stats().dynamicGlyphs == 1, "one dynamic glyph is on screen");
  const auto late = symbolFixture(fixture, 0x0104);
  const SharedFont both[] = {shared(fixture), shared(late)};
  auto *font = ImguiFontSetup::setup(atlas, both, 2, kTest);
  expect(font == overlay.font && font->Sources.Size == 2, "the late face joins the live font");
  for (int i = 0; i < 40; ++i) {
    frame({line});
  }
  const auto after = ImguiFontSetup::stats();
  std::printf("late face with a dynamic glyph on screen: dynamic %d, rebuilds %lld\n", after.dynamicGlyphs, after.rebuilds);
  expect(after.dynamicGlyphs == 1 && after.rebuilds == 0,
         "text that stays on screen causes no rebuild after a face joined");
}

// --- The font feed: one worker makes every platform font call; the render path only reads what it published.
struct FakePlatform {
  int requests = 0, states = 0, addresses = 0, sizes = 0, sleeps = 0, starts = 0;
  int loadsAtPass[ImguiFontFeed::kTypes] = {-1, -1, -1, -1, -1, -1};  // -1: never
  const std::vector<unsigned char> *bytes[ImguiFontFeed::kTypes] = {};
  int pass = 0, stopAfter = 1000000;
  bool canStart = true;
  void (*entry)(void *) = nullptr;
  void *arg = nullptr;
  std::vector<bool> readyAtSleep;  // what the render path answered between two passes
  bool renderWhileAsleep = false;
  int calls() const { return requests + states + addresses + sizes; }
};
FakePlatform g_pl;

const ImguiFontFeed::Platform kFakePlatform = {
    [](int) { g_pl.requests++; },
    [](int type) {
      g_pl.states++;
      return g_pl.loadsAtPass[type] >= 0 && g_pl.pass >= g_pl.loadsAtPass[type];
    },
    [](int type) -> const void * {
      g_pl.addresses++;
      return g_pl.bytes[type]->data();
    },
    [](int type) -> size_t {
      g_pl.sizes++;
      return g_pl.bytes[type]->size();
    },
    []() {
      g_pl.sleeps++;
      g_pl.pass++;
      if (g_pl.renderWhileAsleep) {
        g_pl.readyAtSleep.push_back(ImguiFontFeed::ready(ImGui::GetIO().Fonts, kTest));
      }
      return g_pl.pass < g_pl.stopAfter;
    },
    [](void (*entry)(void *), void *arg) {
      g_pl.starts++;
      g_pl.entry = entry;
      g_pl.arg = arg;
      return g_pl.canStart;
    },
};

void allLoadAtOnce(const std::vector<unsigned char> &fixture) {
  g_pl = FakePlatform{};
  ImguiFontFeed::reset();
  for (int type = 0; type < ImguiFontFeed::kTypes; ++type) {
    g_pl.loadsAtPass[type] = 0;
    g_pl.bytes[type] = &fixture;
  }
}

void test_the_render_path_makes_no_platform_call(const std::vector<unsigned char> &fixture) {
  Overlay overlay({});
  auto *atlas = ImGui::GetIO().Fonts;
  allLoadAtOnce(fixture);
  bool drew = false;
  for (int i = 0; i < 200; ++i) {
    drew |= ImguiFontFeed::ready(atlas, kTest);
  }
  expect(!drew && g_pl.calls() == 0, "before the worker ran, the render path draws nothing and calls nothing");
  const bool started = ImguiFontFeed::startWorker(&kFakePlatform);
  expect(started && g_pl.starts == 1 && g_pl.entry != nullptr, "the worker is started once");
  if (!started || g_pl.entry == nullptr) {
    return;
  }
  expect(g_pl.calls() == 0, "starting the worker makes no platform call on the caller");
  g_pl.entry(g_pl.arg);
  const int worker = g_pl.calls();
  expect(worker > 0 && g_pl.requests == ImguiFontFeed::kTypes, "the worker makes the calls, one request per type");
  bool all = true;
  for (int i = 0; i < 200; ++i) {
    all &= ImguiFontFeed::ready(atlas, kTest);
  }
  expect(all && g_pl.calls() == worker, "200 render passes draw and make no platform call");
  expect(atlas->Fonts.Size == 1 && atlas->Fonts[0]->IsGlyphInFont('H'), "the text face reached the font");
}

void test_fonts_arrive_through_the_mask(const std::vector<unsigned char> &fixture) {
  Overlay overlay({});
  auto *atlas = ImGui::GetIO().Fonts;
  const auto early = symbolFixture(fixture, 0xe000), late = symbolFixture(fixture, 0xe001);
  g_pl = FakePlatform{};
  ImguiFontFeed::reset();
  g_pl.loadsAtPass[5] = 0;  // a symbol face first
  g_pl.bytes[5] = &early;
  g_pl.loadsAtPass[0] = 2;  // then the standard face
  g_pl.bytes[0] = &fixture;
  g_pl.loadsAtPass[2] = 4;  // then another supplementary face
  g_pl.bytes[2] = &late;
  g_pl.stopAfter = 6;
  g_pl.renderWhileAsleep = true;
  if (!ImguiFontFeed::startWorker(&kFakePlatform) || g_pl.entry == nullptr) {
    expect(false, "the worker starts");
    return;
  }
  g_pl.entry(g_pl.arg);
  const auto &log = g_pl.readyAtSleep;
  expect(log.size() == 6 && !log[0] && !log[1] && log[2] && log[5],
         "nothing draws on a symbol face alone; drawing starts once the text face is published");
  expect(ImguiFontFeed::loadedMask() == 0x25u && !ImguiFontFeed::workerDone(), "the mask names the three loaded types");
  ImFont *font = atlas->Fonts.Size == 1 ? atlas->Fonts[0] : nullptr;
  expect(font && font->Sources.Size == 3 && font->Sources[0]->FontData == fixture.data() &&
             font->Sources[1]->FontData == late.data() && font->Sources[2]->FontData == early.data() &&
             font->IsGlyphInFont('H') && font->IsGlyphInFont(0xe000) && font->IsGlyphInFont(0xe001),
         "the published faces are in the font, in query order");
}

void test_the_worker_ends_when_all_six_are_loaded(const std::vector<unsigned char> &fixture) {
  Overlay overlay({});
  g_pl = FakePlatform{};
  ImguiFontFeed::reset();
  for (int type = 0; type < ImguiFontFeed::kTypes; ++type) {
    g_pl.loadsAtPass[type] = type / 2;  // the last two load at the third pass
    g_pl.bytes[type] = &fixture;
  }
  if (!ImguiFontFeed::startWorker(&kFakePlatform) || g_pl.entry == nullptr) {
    expect(false, "the worker starts");
    return;
  }
  g_pl.entry(g_pl.arg);
  expect(ImguiFontFeed::workerDone() && ImguiFontFeed::loadedMask() == 0x3fu && g_pl.sleeps == 2 &&
             g_pl.requests == ImguiFontFeed::kTypes && g_pl.addresses == ImguiFontFeed::kTypes,
         "the worker returns at the pass that finds all six loaded, having asked for each once");
  // A type that never loads: the worker goes on, asleep between passes, until it is told to stop.
  g_pl = FakePlatform{};
  ImguiFontFeed::reset();
  for (int type = 0; type < ImguiFontFeed::kTypes - 1; ++type) {
    g_pl.loadsAtPass[type] = 0;
    g_pl.bytes[type] = &fixture;
  }
  g_pl.stopAfter = 50;
  if (!ImguiFontFeed::startWorker(&kFakePlatform) || g_pl.entry == nullptr) {
    expect(false, "the worker starts again after reset");
    return;
  }
  g_pl.entry(g_pl.arg);
  expect(!ImguiFontFeed::workerDone() && ImguiFontFeed::loadedMask() == 0x1fu && g_pl.sleeps == 50 &&
             g_pl.requests == ImguiFontFeed::kTypes && g_pl.states == 6 + 49,
         "with one type never loading it polls only that type, once per pass, and requests nothing again");
}

void test_no_worker_means_no_overlay_and_no_fallback(const std::vector<unsigned char> &fixture) {
  Overlay overlay({});
  auto *atlas = ImGui::GetIO().Fonts;
  allLoadAtOnce(fixture);
  g_pl.canStart = false;
  const bool started = ImguiFontFeed::startWorker(&kFakePlatform);
  bool drew = false;
  for (int i = 0; i < 200; ++i) {
    drew |= ImguiFontFeed::ready(atlas, kTest);
  }
  expect(!started && g_pl.starts == 1, "a thread that cannot be created is reported");
  expect(!drew && g_pl.calls() == 0 && atlas->Fonts.Size == 0,
         "then the overlay stays off and the render path does not make the calls itself");
}

void test_latin_and_accented_capitals_are_one_height(const std::vector<unsigned char> &fixture) {
  auto font = lineVariant(fixture, 880, -120);
  Overlay overlay({shared(font)});
  if (overlay.font == nullptr) {
    expect(false, "test_latin_and_accented_capitals_are_one_height: font exists");
    return;
  }
  const ImFontGlyph *h = bake(overlay.font)->FindGlyph('H');
  const ImFontGlyph *ae = bake(overlay.font)->FindGlyph(0x00C6);
  std::printf("capital rows H=%.2f..%.2f Æ=%.2f..%.2f\n", h->Y0, h->Y1, ae->Y0, ae->Y1);
  expect(std::fabs(h->Y0 - ae->Y0) <= 1.0f && std::fabs(h->Y1 - ae->Y1) <= 1.0f,
         "test_latin_and_accented_capitals_are_one_height: tops and bottoms match");
}

void test_capitals_keep_their_share_of_the_line(const std::vector<unsigned char> &fixture) {
  for (const auto &[ascent, descent] : {std::pair{1160, -288}, {880, -120}, {1500, -500}}) {
    auto font = lineVariant(fixture, ascent, descent);
    Overlay overlay({shared(font)});
    if (overlay.font == nullptr) {
      expect(false, "test_capitals_keep_their_share_of_the_line: font exists");
      continue;
    }
    const ImFontGlyph *ae = bake(overlay.font)->FindGlyph(0x00C6);
    const float height = ae->Y1 - ae->Y0;
    const float want = 18.0f * 730.0f / 1320.0f;
    std::printf("capital share line=%d height=%.3f target=%.3f\n", ascent - descent, height, want);
    expect(std::fabs(height - want) <= 1.0f,
           "test_capitals_keep_their_share_of_the_line: capital height matches");
  }
  for (const auto &[ascent, descent] : {std::pair{1160, -288}, {880, -120}, {1500, -500}}) {
    auto font = lineVariant(fixture, ascent, descent, false);
    Overlay overlay({shared(font)});
    if (overlay.font == nullptr) {
      expect(false, "test_capitals_keep_their_share_of_the_line: fallback font exists");
      continue;
    }
    const float advance = bake(overlay.font)->FindGlyph(0x30C6)->AdvanceX;
    const float want = 18.0f * 1000.0f / 1320.0f;
    std::printf("em fallback line=%d advance=%.3f target=%.3f\n", ascent - descent, advance, want);
    expect(std::fabs(advance - want) <= 0.5f,
           "test_capitals_keep_their_share_of_the_line: full-width em matches");
  }
}

void test_the_text_keeps_its_place_in_the_line(const std::vector<unsigned char> &fixture) {
  for (const auto &[ascent, descent] : {std::pair{1160, -288}, {880, -120}, {1500, -500}}) {
    auto font = lineVariant(fixture, ascent, descent);
    Overlay overlay({shared(font)});
    if (overlay.font == nullptr) {
      expect(false, "test_the_text_keeps_its_place_in_the_line: font exists");
      continue;
    }
    ImFontBaked *baked = bake(overlay.font);
    const float share = static_cast<float>(ascent) / static_cast<float>(ascent - descent);
    const float scale = (730.0f / 1320.0f) / (733.0f / static_cast<float>(ascent - descent));
    const float target = 18.0f * (1.0f + scale * (2.0f * share - 1.0f)) / 2.0f;
    const ImFontGlyph *h = baked->FindGlyph('H');
    std::printf("baseline line=%d target=%.3f baked-ascent=%.3f offset=%.3f H-bottom=%.3f\n", ascent - descent,
                target, baked->Ascent, overlay.font->Sources[0]->GlyphOffset.y, h->Y1);
    expect(std::fabs(h->Y1 - target) <= 1.0f,
           "test_the_text_keeps_its_place_in_the_line: baseline matches centred target");
    for (const ImFontGlyph &glyph : baked->Glyphs) {
      if (glyph.Visible && ImguiFontSetup::isStaticCodepoint(glyph.Codepoint) &&
          (glyph.Y0 < 0.0f || glyph.Y1 > ImguiFontSetup::kFontSize)) {
        std::printf("static glyph outside line %d: U+%04X %.2f..%.2f\n", ascent - descent, glyph.Codepoint,
                    glyph.Y0, glyph.Y1);
        if (ascent - descent != 2000) {
          const bool descends = glyph.Y1 > h->Y1;
          expect(glyph.Y0 >= 0.0f && glyph.Y1 <= ImguiFontSetup::kFontSize + (descends ? 1.0f : 0.0f),
                 "test_the_text_keeps_its_place_in_the_line: static glyph is inside the line with descender slack");
        }
      }
    }
  }
}

void test_every_source_gets_the_first_sources_em(const std::vector<unsigned char> &fixture) {
  auto first = lineVariant(fixture, 1160, -288);
  auto second = lineVariant(fixture, 880, -120);
  const ImWchar chars[] = {0x30C6, 0x65E5, 0xD55C};
  ImFontGlyph alone[3] = {};
  {
    Overlay overlay({shared(first)});
    if (overlay.font == nullptr) {
      expect(false, "test_every_source_gets_the_first_sources_em: base font exists");
      return;
    }
    for (int i = 0; i < 3; ++i) {
      alone[i] = *bake(overlay.font)->FindGlyph(chars[i]);
    }
  }
  Overlay overlay({shared(first), shared(second)});
  if (overlay.font == nullptr) {
    expect(false, "test_every_source_gets_the_first_sources_em: merged font exists");
    return;
  }
  ImFontConfig *source = nullptr;
  for (ImFontConfig *config : overlay.font->Sources) {
    if (config->FontData == first.data()) {
      source = config;
      break;
    }
  }
  expect(source != nullptr, "the first fixture is present");
  if (source == nullptr) {
    return;
  }
  const ImWchar excluded[] = {0x30C6, 0x30C6, 0x65E5, 0x65E5, 0xD55C, 0xD55C, 0};
  source->GlyphExcludeRanges = static_cast<const ImWchar *>(ImMemdup(excluded, sizeof(excluded)));
  for (int i = 0; i < 3; ++i) {
    const ImFontGlyph *merged = bake(overlay.font)->FindGlyph(chars[i]);
    const float width = merged->X1 - merged->X0, height = merged->Y1 - merged->Y0;
    const float oldWidth = alone[i].X1 - alone[i].X0, oldHeight = alone[i].Y1 - alone[i].Y0;
    std::printf("source em U+%04X alone=%.2fx%.2f adv=%.3f merged=%.2fx%.2f adv=%.3f source=%d\n", chars[i],
                oldWidth, oldHeight, alone[i].AdvanceX, width, height, merged->AdvanceX, merged->SourceIdx);
    expect(merged->Codepoint == chars[i] && merged->SourceIdx == overlay.font->Sources.Size - 1 &&
               std::fabs(width - oldWidth) <= 1.0f && std::fabs(height - oldHeight) <= 1.0f &&
               std::fabs(merged->AdvanceX - alone[i].AdvanceX) <= 0.5f,
           "test_every_source_gets_the_first_sources_em: merged glyph matches first-source glyph");
  }
}

void test_validation_accepts_fonts_and_rejects_anything_else(const std::vector<unsigned char> &fixture) {
  expect(ImguiFontSetup::validFont(fixture.data(), fixture.size()), "the fixture (glyf) is valid");
  expect(!ImguiFontSetup::validFont(nullptr, 100), "null is refused");
  expect(!ImguiFontSetup::validFont(fixture.data(), 0), "an empty region is refused");
  expect(!ImguiFontSetup::validFont(fixture.data(), fixture.size() / 2), "a truncated font is refused");
  std::vector<unsigned char> garbage(4096, 0x5a);
  expect(!ImguiFontSetup::validFont(garbage.data(), garbage.size()), "data that is not an sfnt is refused");
  std::vector<unsigned char> noCmap = fixture;
  for (size_t i = 12; i + 16 <= noCmap.size() && i < 12 + 16 * 64u; i += 16) {
    if (std::memcmp(&noCmap[i], "cmap", 4) == 0) {
      std::memcpy(&noCmap[i], "xmap", 4);
    }
  }
  expect(!ImguiFontSetup::validFont(noCmap.data(), noCmap.size()), "a font without a cmap is refused");
}

// The static set: rasterized in setup(), before any frame, and never rasterized again.
void test_static_set_is_built_once_and_kept(const std::vector<unsigned char> &fixture) {
  Overlay overlay({shared(fixture)});
  ImFontBaked *baked = bake(overlay.font);
  const ImguiFontSetup::Stats before = ImguiFontSetup::stats();
  int loaded = 0, latin = 0;
  std::vector<std::pair<unsigned, ImFontGlyph>> boxes;
  for (unsigned c = 0x20; c <= 0x17F; ++c) {
    if (ImguiFontSetup::isStaticCodepoint(c) && baked->IsGlyphLoaded(static_cast<ImWchar>(c))) {
      loaded += 1;
      latin += c >= 0xA0 ? 1 : 0;
      boxes.emplace_back(c, *baked->FindGlyph(static_cast<ImWchar>(c)));
    }
  }
  std::printf("static set: %d glyphs (%d of U+00A0..U+017F the fixture maps), atlas %dx%d, %lld px packed\n",
              before.staticGlyphs, latin, ImGui::GetIO().Fonts->TexData->Width,
              ImGui::GetIO().Fonts->TexData->Height, livePacked());
  expect(loaded == 95 + 126, "every ASCII character and every Latin-1/Extended-A one a font has is loaded");
  expect(before.staticGlyphs >= loaded, "the static count covers them (plus fallback and ellipsis)");
  expect(before.dynamicGlyphs == 0 && before.dynamicLoads == 0, "no dynamic glyph before any frame");
  expect(baked->FindGlyph('s')->SourceIdx == 0, "ASCII comes from the shared font");
  expect(baked->FindGlyph(0x00C6)->SourceIdx == 0, "Æ comes from the shared font");
  expect(allOwn(overlay.font, "Æsir Ølstue Ærø Ōno Œuvre"), "European names draw from the static set");
  // Many names come and go.
  for (int round = 0; round < 6; ++round) {
    for (int f = 0; f < 10; ++f) {
      frame({kLabel, kanji(round * 8, 8)});
    }
  }
  const ImguiFontSetup::Stats after = ImguiFontSetup::stats();
  int same = 0;
  for (const auto &[c, was] : boxes) {
    const ImFontGlyph *now = bake(overlay.font)->FindGlyph(static_cast<ImWchar>(c));
    same += (now->Codepoint == c && now->AdvanceX == was.AdvanceX && now->X0 == was.X0 && now->Y1 == was.Y1) ? 1 : 0;
  }
  expect(after.rebuilds >= 5, "the dynamic set was rebuilt as the names changed");
  expect(same == loaded, "every static glyph is still loaded, with the same box and advance");
  expect(after.staticGlyphs == before.staticGlyphs, "the static set did not change");
}

void test_shared_fonts_draw_each_character_of_the_names(const std::vector<unsigned char> &fixture) {
  Overlay overlay({shared(fixture)});
  frame({kLabel, "æ Ø é Å ø", "日本語のなまえ", "한글 강철무사", "Иван"});
  expect(allOwn(overlay.font, kLabel), "Æsir テスト draws its own glyphs");
  expect(allOwn(overlay.font, "æØéÅø"), "Latin-1 draws its own glyphs");
  expect(allOwn(overlay.font, "日本語のなまえ"), "kana and kanji draw their own glyphs");
  expect(allOwn(overlay.font, "한글 강철무사"), "Hangul draws its own glyphs");
  expect(allOwn(overlay.font, "Иван"), "Cyrillic draws its own glyphs");
  expect(bake(overlay.font)->FindGlyph(0x30C6)->SourceIdx == 0, "kana comes from the shared font");
  // A character no source has costs that one character.
  frame({"A\xE0\xB8\x81" "B"});
  const auto thai = drawn(overlay.font, "A\xE0\xB8\x81" "B");
  expect(thai.size() == 3 && thai[0].second == 'A' && thai[2].second == 'B', "the characters around it draw");
  expect(thai.size() == 3 && thai[1].second == 0xFFFD, "U+0E01 draws the fallback glyph");
  const ImguiFontSetup::Stats stats = ImguiFontSetup::stats();
  expect(stats.sharedSources == 1 && stats.rejectedSources == 0, "one shared font merged");
  expect(stats.refusals == 0, "nothing refused");
}

void test_fallback_prefers_the_replacement_glyph(const std::vector<unsigned char> &fixture) {
  std::vector<unsigned char> bad(64, 0);
  const auto without = FontFixture::withoutReplacement(fixture);
  expect(!without.empty(), "fixture without replacement glyph was made");
  Overlay overlay({shared(bad), shared(without), shared(fixture)});
  expect(overlay.font->FallbackChar == 0xFFFD, "U+FFFD becomes the fallback when a source has it");
  frame({"A\xE0\xB8\x81"});
  const auto thai = drawn(overlay.font, "A\xE0\xB8\x81");
  expect(thai.size() == 2 && thai[1].second == 0xFFFD, "a missing character draws U+FFFD");
  const ImguiFontSetup::Stats stats = ImguiFontSetup::stats();
  expect(stats.sharedSources == 2 && stats.rejectedSources == 1, "the invalid region is skipped, not fatal");
}

void test_fallback_without_replacement_is_question_mark(const std::vector<unsigned char> &fixture) {
  const auto without = FontFixture::withoutReplacement(fixture);
  expect(!without.empty(), "fixture without replacement glyph was made");
  Overlay overlay({shared(without)});
  frame({"A\xE0\xB8\x81" "B"});
  const auto thai = drawn(overlay.font, "A\xE0\xB8\x81" "B");
  expect(overlay.font->FallbackChar == '?' && thai.size() == 3 && thai[1].second == '?',
         "a missing glyph draws question mark without U+FFFD");
}

void test_font_retry_is_spaced_and_stops() {
  int attempts = 0;
  for (unsigned frame = 1; frame <= 180; ++frame) {
    const bool due = ImguiFontRetry::due(frame, false);
    expect(due == (frame == 60 || frame == 120 || frame == 180), "retry is once per 60 frames");
    attempts += due ? 1 : 0;
    expect(!ImguiFontRetry::due(frame, true), "retry stops when the font is ready");
  }
  expect(!ImguiFontRetry::due(0, false) && attempts == 3, "no zero-frame retry and three spaced attempts");
  std::printf("font retry: attempts at 60, 120, 180; none after ready\n");
}

// The dynamic set holds what is on screen: the union of the drawn characters outside the static set.
void test_dynamic_set_is_the_visible_set(const std::vector<unsigned char> &fixture) {
  Overlay overlay({shared(fixture)});
  const std::string a = "日本語のなまえ", b = "강철무사";
  for (int f = 0; f < 3; ++f) {
    frame({kLabel, a});
  }
  frame({kLabel, a});
  expect(dynamicSet() == unite(dynamicOf(kLabel), dynamicOf(a)), "label and name A: their characters exactly");
  for (int f = 0; f < 10; ++f) {
    frame({kLabel, a, b});
  }
  expect(dynamicSet() == unite(unite(dynamicOf(kLabel), dynamicOf(a)), dynamicOf(b)),
         "A and B both on screen: both sets, nothing dropped");
  expect(ImguiFontSetup::stats().rebuilds == 0, "a name added beside another needs no rebuild");
  for (int f = 0; f < 10; ++f) {
    frame({kLabel, b});
  }
  frame({kLabel, b});
  expect(dynamicSet() == unite(dynamicOf(kLabel), dynamicOf(b)), "A left the screen: only the label and B remain");
  expect(ImguiFontSetup::stats().dynamicVisible == static_cast<int>(dynamicSet().size()),
         "every dynamic glyph in the atlas is on screen");
  // A line cut by the window edge: ImGui rasterizes the characters of a line it draws, past the edge too, so they
  // stay with the line; only the ones drawn count as on screen, and the steady line causes no rebuild.
  const std::string wide = kanji(100, 30);
  for (int f = 0; f < 12; ++f) {
    frame({wide}, 0.0f, 150.0f);
  }
  frame({wide}, 0.0f, 150.0f);
  const std::set<unsigned> clipped = dynamicSet();
  const int drawnNow = ImguiFontSetup::stats().dynamicVisible;
  std::printf("clipped: %d of the line's %zu characters drawn in a 150 px window, %zu held\n", drawnNow,
              dynamicOf(wide).size(), clipped.size());
  expect(clipped == dynamicOf(wide), "the line on screen: its characters, the label's gone");
  expect(drawnNow > 0 && drawnNow < static_cast<int>(clipped.size()), "only the drawn part counts as visible");
  const long long rebuilds = ImguiFontSetup::stats().rebuilds;
  for (int f = 0; f < 30; ++f) {
    frame({wide}, 0.0f, 150.0f);
  }
  expect(ImguiFontSetup::stats().rebuilds == rebuilds, "a steadily clipped line causes no further rebuild");
  expect(dynamicSet() == clipped, "and keeps the same set");
}

// Rebuilds happen when what is on screen changes, never per frame.
void test_rebuild_only_when_the_screen_changes(const std::vector<unsigned char> &fixture) {
  Overlay overlay({shared(fixture)});
  for (int f = 0; f < 300; ++f) {
    frame({kLabel, "Opp: 日本語のなまえ | 4 (12ms)"});
  }
  ImguiFontSetup::Stats stats = ImguiFontSetup::stats();
  expect(stats.rebuilds == 0, "300 frames of the same text: no rebuild");
  expect(stats.dynamicLoads == static_cast<long long>(unite(dynamicOf(kLabel), dynamicOf("日本語のなまえ")).size()),
         "each dynamic glyph was rasterized once");
  // Hidden for less than the grace period (a page flip): nothing dropped.
  for (int f = 0; f < 3; ++f) {
    frame({"Performance"});
  }
  for (int f = 0; f < 20; ++f) {
    frame({kLabel, "Opp: 日本語のなまえ | 4 (12ms)"});
  }
  expect(ImguiFontSetup::stats().rebuilds == 0, "a short hide does not rebuild");
  // Hidden for longer: one rebuild drops the names; shown again: they load again, no rebuild.
  for (int f = 0; f < 20; ++f) {
    frame({"Performance"});
  }
  expect(ImguiFontSetup::stats().rebuilds == 1, "names that left the screen: one rebuild");
  expect(dynamicSet().empty(), "and no dynamic glyph is held while none is on screen");
  for (int f = 0; f < 20; ++f) {
    frame({kLabel});
  }
  expect(ImguiFontSetup::stats().rebuilds == 1, "the name coming back loads it, without a rebuild");
  expect(allOwn(overlay.font, kLabel), "and it draws its own glyphs");
  // Text measured every frame but never drawn does not keep the set rebuilding.
  const long long rebuilds = ImguiFontSetup::stats().rebuilds;
  for (int f = 0; f < 120; ++f) {
    frame({kLabel}, 0.0f, 1200.0f, {"한글 강철무사"});
  }
  expect(ImguiFontSetup::stats().rebuilds == rebuilds, "measured-only text: no rebuild loop");
  expect(dynamicSet() == dynamicOf(kLabel), "and measuring rasterizes nothing: only the drawn label is held");
}

// Glyphs that left the screen are gone from the atlas, the glyph table and the index after the rebuild.
void test_glyphs_that_left_are_dropped(const std::vector<unsigned char> &fixture) {
  Overlay overlay({shared(fixture)});
  const int staticGlyphs = ImguiFontSetup::stats().staticGlyphs;
  const long long staticPacked = livePacked();
  const std::string a = kanji(0, 40), b = "Иван";
  for (int f = 0; f < 4; ++f) {
    frame({a});
  }
  const long long withA = livePacked();
  const long long indexWithA = ImguiFontSetup::stats().indexBytes;
  const bool kanjiPage = pageAllocated(bake(overlay.font), 0x4E);
  const ImTextureData *texWithA = ImGui::GetIO().Fonts->TexData;
  for (int f = 0; f < 12; ++f) {
    frame({b});
  }
  ImFontBaked *baked = bake(overlay.font);
  int stillLoaded = 0;
  for (unsigned c : dynamicOf(a)) {
    stillLoaded += baked->IsGlyphLoaded(static_cast<ImWchar>(c)) ? 1 : 0;
  }
  std::printf("dropped: packed %lld px static, %lld with 40 kanji, %lld after they left; index %lld -> %lld B\n",
              staticPacked, withA, livePacked(), indexWithA, ImguiFontSetup::stats().indexBytes);
  expect(ImguiFontSetup::stats().rebuilds == 1, "one rebuild");
  expect(stillLoaded == 0, "none of A's glyphs is loaded any more");
  expect(baked->Glyphs.Size == staticGlyphs + static_cast<int>(dynamicOf(b).size()),
         "the glyph table holds the static set and B only");
  expect(livePacked() < withA && livePacked() < staticPacked + 4 * 800, "their atlas space is reclaimed");
  if (g_paged) {
    expect(kanjiPage && !pageAllocated(baked, 0x4E) && pageAllocated(baked, 0x04),
           "the kanji's index page is freed, B's allocated");
  } else {
    const unsigned highest = std::max({*dynamicOf(b).rbegin(), static_cast<unsigned>(overlay.font->FallbackChar),
                                       static_cast<unsigned>(overlay.font->EllipsisChar), 0x17Fu});
    expect(baked->IndexLookup.Size <= static_cast<int>(highest) + 1, "the index shrank to the retained range");
  }
  expect(ImGui::GetIO().Fonts->TexData != texWithA, "the rebuild repacked into a new texture");
  expect(allOwn(overlay.font, b.c_str()) && allOwn(overlay.font, kLabel), "what is on screen still draws right");
}

// The rebuild runs only between frames, through the texture requests: a new texture is created, and the old one is
// destroyed by the backend only after the frames that sampled it.
void test_frame_boundary_rule(const std::vector<unsigned char> &fixture) {
  Overlay overlay({shared(fixture)});
  g_destroyDelay = 2;
  g_destroyed.clear();
  for (int f = 0; f < 3; ++f) {
    frame({kLabel, "日本語のなまえ"});
  }
  // Seeded violation: frameBoundary() placed inside a frame is refused and changes nothing.
  ImGuiIO &io = ImGui::GetIO();
  io.DeltaTime = 1.0f / 60.0f;
  ImGui::NewFrame();
  ImGui::Begin("Opp");
  ImGui::TextUnformatted("강철무사");
  const ImTextureData *tex = io.Fonts->TexData;
  const int glyphs = bake(overlay.font)->Glyphs.Size;
  for (int i = 0; i < 20; ++i) {
    expect(!ImguiFontSetup::frameBoundary(io.Fonts, nullptr), "inside a frame, frameBoundary() does nothing");
  }
  ImGui::TextUnformatted(kLabel);
  ImGui::End();
  ImGui::Render();
  honourTextures();
  expect(ImguiFontSetup::stats().misplaced == 20, "each misplaced call is counted");
  expect(io.Fonts->TexData == tex && bake(overlay.font)->Glyphs.Size >= glyphs, "and the frame's glyphs stay put");
  // Names changing for 400 frames.
  const char *names[] = {"日本語のなまえ", "강철무사", "Иван", "Ølstue", "テスト日本", "한글"};
  int rebuilds = 0, insideFrame = 0, deadTextures = 0, oldAtlas = 0, wrongGlyphs = 0, maxTextures = 0;
  for (int f = 0; f < 400; ++f) {
    const char *name = names[(f / 25) % 6];
    const FrameNote note = frame({kLabel, name});
    rebuilds += note.rebuiltAtBoundary ? 1 : 0;
    insideFrame += note.changedInsideFrame ? 1 : 0;
    deadTextures += note.texturesLive ? 0 : 1;
    oldAtlas += note.onCurrentAtlas ? 0 : 1;
    wrongGlyphs += (allOwn(overlay.font, name) && allOwn(overlay.font, kLabel)) ? 0 : 1;
    maxTextures = std::max(maxTextures, io.Fonts->TexList.Size);
  }
  std::printf("boundary: %d rebuilds in 400 frames, %d textures destroyed, at most %d textures held\n", rebuilds,
              static_cast<int>(g_destroyed.size()), maxTextures);
  expect(rebuilds >= 10, "the names that left were dropped as they changed");
  expect(static_cast<long long>(rebuilds) == ImguiFontSetup::stats().rebuilds, "every rebuild was at a boundary");
  expect(insideFrame == 0, "no rebuild and no texture change inside a frame");
  expect(deadTextures == 0, "no frame drew with a texture the backend had destroyed");
  expect(oldAtlas == 0, "after a rebuild every frame draws from the new texture, never the one being retired");
  expect(wrongGlyphs == 0, "every frame's names drew their own glyphs");
  expect(maxTextures <= 4, "the atlas texture and the ones awaiting the backend, no more");
  g_logical[g_paged ? 1 : 0] = logicalIndex(bake(overlay.font));
  g_glyphTable[g_paged ? 1 : 0] = glyphTable(bake(overlay.font));
  g_destroyDelay = 0;
}

// Memory stays bounded however many names pass: the static set plus what is on screen.
void test_bounded_bytes(const std::vector<unsigned char> &fixture) {
  Overlay overlay({shared(fixture)});
  std::vector<unsigned> pool;
  for (unsigned c = 0x4E00; c < 0x4EA0; ++c) {
    pool.push_back(c);
  }
  for (unsigned c : dynamicOf("한글강철무사日本語のなまえテストИван")) {
    pool.push_back(c);
  }
  unsigned seed = 12345;
  auto next = [&seed] {
    seed = seed * 1103515245u + 12345u;
    return (seed >> 16) & 0x7FFF;
  };
  int peakPixels[2] = {}, peakDynamic[2] = {}, peakGlyphs[2] = {};
  long long peakIndex[2] = {};
  std::string name;
  for (int f = 0; f < 3000; ++f) {
    if (f % 15 == 0) {
      std::vector<unsigned> codepoints;
      for (int i = 0; i < 6; ++i) {
        codepoints.push_back(pool[next() % pool.size()]);
      }
      name = utf8Of(codepoints);
    }
    frame({kLabel, name});
    const int half = f < 1500 ? 0 : 1;
    const ImguiFontSetup::Stats stats = ImguiFontSetup::stats();
    peakPixels[half] = std::max(peakPixels[half], texturePixelBytes());
    peakDynamic[half] = std::max(peakDynamic[half], stats.dynamicGlyphs);
    peakGlyphs[half] = std::max(peakGlyphs[half], bake(overlay.font)->Glyphs.Size);
    peakIndex[half] = std::max(peakIndex[half], stats.indexBytes);
  }
  const ImguiFontSetup::Stats stats = ImguiFontSetup::stats();
  std::printf("bounded: 200 names over 3000 frames, %lld rebuilds, %lld dynamic loads; peaks first/second half: "
              "texture pixels %d/%d B, dynamic glyphs %d/%d, glyph table %d/%d, index %lld/%lld B\n",
              stats.rebuilds, stats.dynamicLoads, peakPixels[0], peakPixels[1], peakDynamic[0], peakDynamic[1],
              peakGlyphs[0], peakGlyphs[1], peakIndex[0], peakIndex[1]);
  expect(stats.rebuilds >= 150, "the names that left were dropped");
  expect(peakDynamic[1] <= 3 + 2 * 6, "at most the label and two names' glyphs at a time");
  expect(peakGlyphs[1] <= stats.staticGlyphs + 3 + 2 * 6, "the glyph table does not grow");
  expect(peakPixels[1] <= peakPixels[0], "texture memory in the second half no higher than in the first");
  // Dense: an entry through the highest glyph or fallback; paged: one page per block, plus the glyph table.
  const long long highest = std::max(0xD55CLL, static_cast<long long>(overlay.font->FallbackChar));
  long long indexBound = (highest + 1) * 6 + 16 * 1024;
  if (g_paged) {
    std::set<unsigned> pages = {0x00, 0x01, static_cast<unsigned>(overlay.font->FallbackChar) >> 8,
                                static_cast<unsigned>(overlay.font->EllipsisChar) >> 8};
    for (unsigned c : pool) {
      pages.insert(c >> 8);
    }
    for (unsigned c : dynamicOf(kLabel)) {
      pages.insert(c >> 8);
    }
    indexBound = IM_FONTBAKED_INDEX_PAGE_COUNT * 2 + static_cast<long long>(pages.size()) * 256 * 6 + 24 * 1024;
    std::printf("bounded: paged bound %lld B (%zu pages possible)\n", indexBound, pages.size());
  }
  expect(peakIndex[0] <= indexBound && peakIndex[1] <= indexBound, "index memory stays within its bound");
  expect(peakPixels[1] <= 3 * 512 * 256, "at most the current atlas and textures awaiting destruction");
  expect(allOwn(overlay.font, kLabel) && allOwn(overlay.font, name.c_str()), "the last frame draws right");
}

void test_atlas_memory_with_the_label(const std::vector<unsigned char> &fixture) {
  Overlay overlay({shared(fixture)});
  for (int f = 0; f < 3; ++f) {
    frame({kLabel, "Opp: LLUltra | 4 (12ms)"});
  }
  ImTextureData *tex = ImGui::GetIO().Fonts->TexData;
  const int bytes = tex->GetSizeInBytes();
  const int legacy = 512 * 128 * 4;
  const ImguiFontSetup::Stats stats = ImguiFontSetup::stats();
  std::printf("memory: legacy atlas 512x128 RGBA32 %d B; now %dx%d bpp %d %d B; static %d glyphs, dynamic %d; "
              "index %lld B; packed %lld px\n",
              legacy, tex->Width, tex->Height, tex->BytesPerPixel, bytes, stats.staticGlyphs, stats.dynamicGlyphs,
              stats.indexBytes, livePacked());
  expect(tex->Format == ImTextureFormat_Alpha8, "the atlas is Alpha8");
  expect(bytes <= legacy / 2, "with the static set and the label, at most half of the legacy atlas");
  expect(stats.dynamicGlyphs == 3, "the label's three kana are the dynamic set");
}

// stats().indexBytes (the figure renderer-status.txt reports: the glyph table and the per-codepoint index) for three
// screens in turn, the previous screen's names dropped first: Latin text, the kana label, a Hangul and a kanji name
// together. main() sets the two modes side by side.
void test_index_bytes_by_language(const std::vector<unsigned char> &fixture) {
  Overlay overlay({shared(fixture)});
  const std::vector<std::string> screens[kScreens] = {
      {"Opp: LLUltra | 4 (12ms)"}, {"Opp: Æsir テスト | 4 (12ms)"}, {"Opp: 강철무사 | 4 (12ms)", "日本語のなまえ"}};
  const int mode = g_paged ? 1 : 0;
  for (int i = 0; i < kScreens; ++i) {
    for (int f = 0; f < 14; ++f) {
      frame(screens[i]);
    }
    const ImguiFontSetup::Stats stats = ImguiFontSetup::stats();
    const ImFontBaked *baked = bake(overlay.font);
    std::set<unsigned> want;
    for (const std::string &line : screens[i]) {
      want = unite(want, dynamicOf(line));
    }
    g_bytes[mode][i] = stats.indexBytes;
    g_codepointBytes[mode][i] = static_cast<long long>(baked->IndexLookup.Capacity) * 2 +
                                static_cast<long long>(baked->IndexAdvanceX.Capacity) * 4;
    g_pages[mode][i] = stats.indexPages;
    expect(stats.indexPaged == g_paged, "the bake's index mode is the one asked for");
    expect(dynamicSet() == want, "the screen's names are the dynamic set");
    if (g_paged) {
      expect(g_codepointBytes[mode][i] == IM_FONTBAKED_INDEX_PAGE_COUNT * 2 + stats.indexPages * 256 * 6,
             "the paged index is the page map and its pages, nothing more");
    }
  }
}

void test_other_sizes_scale_the_one_bake(const std::vector<unsigned char> &fixture) {
  Overlay overlay({shared(fixture)});
  frame({kLabel, "日本"});
  frame({kLabel}, 13.5f);
  frame({kLabel}, 22.0f);
  for (int f = 0; f < 12; ++f) {
    frame({"한글"}, 13.5f);
  }
  expect(ImguiFontSetup::stats().rebuilds >= 1, "a rebuild happened");
  expect(ImGui::GetIO().Fonts->Builder->BakedPool.Size == 1, "the compact strip's sizes add no bake");
  expect(overlay.font->GetFontBaked(13.5f)->Size == ImguiFontSetup::kFontSize, "13.5 px scales the 18 px bake");
}

// A frame whose own text would overflow the fill guard draws the fallback for the excess, then recovers.
void test_fill_guard_keeps_packing_inside_the_largest_atlas(const std::vector<unsigned char> &fixture) {
  const auto without = FontFixture::withoutReplacement(fixture);
  expect(!without.empty(), "fixture without replacement glyph was made");
  const Limits limits = {0.2f, 256, 128, 512, 512, 6, 1, 30};
  Overlay overlay({shared(without)}, limits);
  const std::string all = kanji(0, 160);
  for (int f = 0; f < 3; ++f) {
    frame({all});
  }
  ImguiFontSetup::Stats stats = ImguiFontSetup::stats();
  ImTextureData *tex = ImGui::GetIO().Fonts->TexData;
  std::printf("fill guard: packed %lld px of %d, dynamic %d, refusals %lld, atlas %dx%d\n", livePacked(), 512 * 512,
              stats.dynamicGlyphs, stats.refusals, tex->Width, tex->Height);
  expect(stats.dynamicGlyphs > 0 && stats.refusals > 0, "some kanji drawn, the excess refused");
  expect(livePacked() <= 512 * 512 / 5, "packing stays within the fill limit of the largest atlas");
  expect(tex->Width <= 512 && tex->Height <= 512, "the atlas never exceeds its maximum");
  for (int f = 0; f < 12; ++f) {
    frame({kLabel});
  }
  expect(allOwn(overlay.font, kLabel), "once that text is gone, the label draws its own glyphs");
}

void test_a_refused_glyph_draws_after_the_rebuild_with_either_fallback(const std::vector<unsigned char> &fixture) {
  const auto without = FontFixture::withoutReplacement(fixture);
  expect(!without.empty(), "fixture without replacement glyph was made");
  for (bool replacement : {false, true}) {
    const Limits limits = {0.2f, 256, 128, 512, 512, 6, 1, 30};
    Overlay overlay({shared(replacement ? fixture : without)}, limits);
    const std::string all = kanji(0, 160);
    for (int f = 0; f < 3; ++f) {
      frame({all});
    }
    frame({kLabel});
    const ImFontGlyph *refused = bake(overlay.font)->FindGlyph(0x30C6);
    const unsigned before = refused->Codepoint;
    const long long refusals = ImguiFontSetup::stats().refusals;
    for (int f = 0; f < 11; ++f) {
      frame({kLabel});
    }
    const ImFontGlyph *after = bake(overlay.font)->FindGlyph(0x30C6);
    const ImguiFontSetup::Stats stats = ImguiFontSetup::stats();
    std::printf("rebuild recovery fallback=U+%04X before=U+%04X after=U+%04X refusals=%lld rebuilds=%lld\n",
                overlay.font->FallbackChar, before, after->Codepoint, refusals, stats.rebuilds);
    expect(refusals > 0 && before == overlay.font->FallbackChar,
           "test_a_refused_glyph_draws_after_the_rebuild_with_either_fallback: glyph refused");
    expect(stats.rebuilds > 0 && after->Codepoint == 0x30C6,
           "test_a_refused_glyph_draws_after_the_rebuild_with_either_fallback: glyph recovered");
  }
}

// 160 kanji in one frame into an atlas capped at 256x256, in a child process. Returns its wait status.
int overfill(const std::vector<unsigned char> &fixture, float fillLimit) {
  std::fflush(stdout);
  std::fflush(stderr);
  const pid_t child = fork();
  if (child == 0) {
    const Limits limits = {fillLimit, 256, 128, 256, 256, 6, 1, 30};
    Overlay overlay({shared(fixture)}, limits);
    for (int batch = 0; batch < 8; ++batch) {
      frame({kanji(0, 160)});
    }
    _exit(0);
  }
  int status = 0;
  waitpid(child, &status, 0);
  return status;
}

// Seeded violation: the same overfill with the guard on exits normally; with it off, ImGui's
// "Out of texture memory" assert aborts the process, as SMASH_ASSERT would abort the game.
void test_without_the_fill_guard_imgui_aborts(const std::vector<unsigned char> &fixture) {
  const int guarded = overfill(fixture, 0.5f);
  expect(WIFEXITED(guarded) && WEXITSTATUS(guarded) == 0, "with the guard the overfill exits normally");
  const int unguarded = overfill(fixture, 64.0f);
  expect(WIFSIGNALED(unguarded) && WTERMSIG(unguarded) == SIGABRT, "without the guard the atlas overflows and aborts");
}

// The production limits: the draw data is read every fourth frame and a glyph leaves after 60 frames.
void test_default_limits_drop_a_name_within_about_a_second(const std::vector<unsigned char> &fixture) {
  Overlay overlay({shared(fixture)}, ImguiFontSetup::kDefaultLimits);
  for (int f = 0; f < 20; ++f) {
    frame({kLabel});
  }
  int when = -1;
  for (int f = 0; f < 120; ++f) {
    if (frame({"강철무사"}).rebuiltAtBoundary && when < 0) {
      when = f;
    }
  }
  frame({"강철무사"});
  std::printf("default limits: the label's glyphs dropped %d frames after it left\n", when);
  expect(when >= 56 && when <= 68, "dropped about 60 frames (one scan interval either way) after leaving");
  expect(ImguiFontSetup::stats().rebuilds == 1, "in one rebuild");
  expect(dynamicSet() == dynamicOf("강철무사"), "leaving the name on screen");
}

}  // namespace

int main(int argc, char **argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc < 2) {
    std::fprintf(stderr, "usage: font_setup_test <label-fixture.ttf>\n");
    return 2;
  }
  const std::vector<unsigned char> fixture = readFile(argv[1]);
  if (fixture.empty()) {
    std::fprintf(stderr, "font files not readable\n");
    return 2;
  }
  for (const bool paged : {false, true}) {
    g_paged = paged;
    ImguiFontSetup::usePagedIndex(paged);
    std::printf("== %s per-codepoint index\n", paged ? "paged" : "dense");
    test_the_shared_fonts_are_the_only_sources(fixture);
    test_no_valid_shared_font_adds_no_font();
    test_symbol_only_does_not_make_the_overlay_ready(fixture);
    test_late_shared_face_joins_the_live_font_once(fixture);
    test_the_base_is_chosen_by_font_order_not_arrival(fixture);
    test_a_late_face_adding_a_static_glyph_draws_without_a_rebuild(fixture);
    test_a_late_face_keeps_the_dynamic_tracking_aligned(fixture);
    test_the_render_path_makes_no_platform_call(fixture);
    test_fonts_arrive_through_the_mask(fixture);
    test_the_worker_ends_when_all_six_are_loaded(fixture);
    test_no_worker_means_no_overlay_and_no_fallback(fixture);
    test_latin_and_accented_capitals_are_one_height(fixture);
    test_capitals_keep_their_share_of_the_line(fixture);
    test_the_text_keeps_its_place_in_the_line(fixture);
    test_every_source_gets_the_first_sources_em(fixture);
    test_font_retry_is_spaced_and_stops();
    test_validation_accepts_fonts_and_rejects_anything_else(fixture);
    test_static_set_is_built_once_and_kept(fixture);
    test_shared_fonts_draw_each_character_of_the_names(fixture);
    test_fallback_prefers_the_replacement_glyph(fixture);
    test_fallback_without_replacement_is_question_mark(fixture);
    test_dynamic_set_is_the_visible_set(fixture);
    test_rebuild_only_when_the_screen_changes(fixture);
    test_glyphs_that_left_are_dropped(fixture);
    test_frame_boundary_rule(fixture);
    test_bounded_bytes(fixture);
    test_atlas_memory_with_the_label(fixture);
    test_index_bytes_by_language(fixture);
    test_other_sizes_scale_the_one_bake(fixture);
    test_fill_guard_keeps_packing_inside_the_largest_atlas(fixture);
    test_a_refused_glyph_draws_after_the_rebuild_with_either_fallback(fixture);
    test_without_the_fill_guard_imgui_aborts(fixture);
    test_default_limits_drop_a_name_within_about_a_second(fixture);
  }
  ImguiFontSetup::usePagedIndex(false);
  g_paged = false;

  // The two modes side by side.
  expect(!g_logical[0].empty() && g_logical[0] == g_logical[1],
         "after 400 frames of name changes and rebuilds, the paged index answers as the dense one for every codepoint");
  expect(!g_glyphTable[0].empty() && g_glyphTable[0] == g_glyphTable[1], "and the glyph tables are identical");
  std::printf("index bytes (stats().indexBytes: glyph table + codepoint index; codepoint index alone in brackets):\n");
  for (int i = 0; i < kScreens; ++i) {
    std::printf("  %-26s dense %7lld B [%7lld B]   paged %7lld B [%6lld B, %d pages]\n", kScreenNames[i],
                g_bytes[0][i], g_codepointBytes[0][i], g_bytes[1][i], g_codepointBytes[1][i], g_pages[1][i]);
  }
  expect(g_bytes[1][0] <= g_bytes[0][0] + 2048, "Latin only: paged costs at most the page map and a page more");
  expect(g_bytes[1][1] < g_bytes[0][1], "the kana label: paged is smaller");
  expect(g_bytes[1][2] * 5 < g_bytes[0][2], "Hangul and kanji: paged is a fifth of dense or less");
  if (g_failures == 0) {
    std::printf("font_setup_test: ok\n");
  }
  return g_failures == 0 ? 0 : 1;
}
