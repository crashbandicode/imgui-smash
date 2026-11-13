// Host tests of the overlay's fonts (src/imgui_backend/font_setup.cpp) through ImGui's own text path:
// ImGui::TextUnformatted in a window, as Deluxe draws its labels, in the host's frame loop (procDraw):
// frameBoundary() with the last frame's draw data, NewFrame, the windows, Render, then the backend's side of
// ImGuiBackendFlags_RendererHasTextures. A drawn character's glyph is what ImFont::RenderText draws for it:
// baked->FindGlyph() of the codepoint ImTextCharFromUtf8 decodes.
//
// Usage: font_setup_test <host-tests/fixtures/label-fixture.ttf> <fonts/JetBrainsMonoNL-Regular.ttf>
#include "imgui_backend/font_setup.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "JetBrainsMonoNL-Regular.h"

#include <algorithm>
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

void test_validation_accepts_fonts_and_rejects_anything_else(const std::vector<unsigned char> &fixture,
                                                             const std::vector<unsigned char> &jetbrains) {
  expect(ImguiFontSetup::validFont(fixture.data(), fixture.size()), "the fixture (glyf) is valid");
  expect(ImguiFontSetup::validFont(jetbrains.data(), jetbrains.size()), "full JetBrains Mono is valid");
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

// The deluxe-branch setup: JetBrains Mono's ASCII subset alone. Every non-ASCII character draws '?'.
void test_main_font_alone_draws_question_marks_for_the_name() {
  Overlay overlay({});
  frame({"Æsir テスト日本", "æ Ø é"});
  int fallback = 0;
  for (auto [c, glyph] : drawn(overlay.font, "Æsir テスト日本")) {
    if (c >= 0x80) {
      fallback += glyph == '?' ? 1 : 0;
    } else {
      expect(c == glyph, "ASCII draws itself with the main font alone");
    }
  }
  expect(fallback == 6, "Æ and the five kana/kanji each draw '?' without a shared font");
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
  expect(baked->FindGlyph('s')->SourceIdx == 0, "ASCII comes from JetBrains Mono");
  expect(baked->FindGlyph(0x00C6)->SourceIdx == 1, "Æ comes from the shared font");
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
  expect(bake(overlay.font)->FindGlyph(0x30C6)->SourceIdx == 1, "kana comes from the shared font");
  // A character no source has costs that one character.
  frame({"A\xE0\xB8\x81" "B"});
  const auto thai = drawn(overlay.font, "A\xE0\xB8\x81" "B");
  expect(thai.size() == 3 && thai[0].second == 'A' && thai[2].second == 'B', "the characters around it draw");
  expect(thai.size() == 3 && thai[1].second == '?', "U+0E01 draws the fallback glyph");
  const ImguiFontSetup::Stats stats = ImguiFontSetup::stats();
  expect(stats.sharedSources == 1 && stats.rejectedSources == 0, "one shared font merged");
  expect(stats.refusals == 0, "nothing refused");
}

void test_fallback_prefers_the_replacement_glyph(const std::vector<unsigned char> &fixture,
                                                 const std::vector<unsigned char> &jetbrains) {
  std::vector<unsigned char> bad(64, 0);
  Overlay overlay({shared(bad), shared(fixture), shared(jetbrains)});
  expect(overlay.font->FallbackChar == 0xFFFD, "U+FFFD becomes the fallback when a source has it");
  frame({"A\xE0\xB8\x81"});
  const auto thai = drawn(overlay.font, "A\xE0\xB8\x81");
  expect(thai.size() == 2 && thai[1].second == 0xFFFD, "a missing character draws U+FFFD");
  const ImguiFontSetup::Stats stats = ImguiFontSetup::stats();
  expect(stats.sharedSources == 2 && stats.rejectedSources == 1, "the invalid region is skipped, not fatal");
}

// ASCII glyph boxes and advances match the legacy RGBA32 atlas the deluxe branch built, so layouts do not move.
void test_ascii_glyphs_match_the_legacy_atlas(const std::vector<unsigned char> &fixture) {
  ImFontGlyph legacy[95] = {};
  {
    ImGuiContext *context = ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60.0f;
    // c185303 InitBackend's font call and setupFont's upload.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.Fonts->AddFontFromMemoryCompressedTTF(JetBrainsMonoNL_compressed_data, JetBrainsMonoNL_compressed_size, 18.f);
    unsigned char *pixels = nullptr;
    int width = 0, height = 0, bpp = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bpp);
    ImGui::NewFrame();
    ImFontBaked *baked = ImGui::GetFontBaked();
    for (int c = 0x20; c < 0x7F; ++c) {
      legacy[c - 0x20] = *baked->FindGlyph(static_cast<ImWchar>(c));
    }
    ImGui::EndFrame();
    ImGui::DestroyContext(context);
  }
  Overlay overlay({shared(fixture)});
  std::string ascii;
  for (int c = 0x20; c < 0x7F; ++c) {
    ascii += static_cast<char>(c);
  }
  frame({ascii});
  ImFontBaked *baked = bake(overlay.font);
  int same = 0;
  for (int c = 0x20; c < 0x7F; ++c) {
    const ImFontGlyph *now = baked->FindGlyph(static_cast<ImWchar>(c));
    const ImFontGlyph &was = legacy[c - 0x20];
    same += (now->AdvanceX == was.AdvanceX && now->X0 == was.X0 && now->X1 == was.X1 && now->Y0 == was.Y0 &&
             now->Y1 == was.Y1)
                ? 1
                : 0;
  }
  expect(same == 95, "all 95 ASCII glyphs keep their legacy box and advance");
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
    expect(baked->IndexLookup.Size <= static_cast<int>(*dynamicOf(b).rbegin()) + 1, "the index shrank to B's range");
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
  // Dense: an index entry per codepoint up to the largest one loaded (here U+D55C); frameBoundary() trims the spare
  // capacity ImGui's growth leaves. Paged: the page map and one 256-codepoint page per block the names use. Both plus
  // the glyph table.
  long long indexBound = (0xD55CLL + 1) * 6 + 16 * 1024;
  if (g_paged) {
    std::set<unsigned> pages = {0x00, 0x01};
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
  const Limits limits = {0.2f, 256, 128, 512, 512, 6, 1, 30};
  Overlay overlay({shared(fixture)}, limits);
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
  if (argc < 3) {
    std::fprintf(stderr, "usage: font_setup_test <label-fixture.ttf> <JetBrainsMonoNL-Regular.ttf>\n");
    return 2;
  }
  const std::vector<unsigned char> fixture = readFile(argv[1]);
  const std::vector<unsigned char> jetbrains = readFile(argv[2]);
  if (fixture.empty() || jetbrains.empty()) {
    std::fprintf(stderr, "font files not readable\n");
    return 2;
  }
  for (const bool paged : {false, true}) {
    g_paged = paged;
    ImguiFontSetup::usePagedIndex(paged);
    std::printf("== %s per-codepoint index\n", paged ? "paged" : "dense");
    test_validation_accepts_fonts_and_rejects_anything_else(fixture, jetbrains);
    test_main_font_alone_draws_question_marks_for_the_name();
    test_static_set_is_built_once_and_kept(fixture);
    test_shared_fonts_draw_each_character_of_the_names(fixture);
    test_fallback_prefers_the_replacement_glyph(fixture, jetbrains);
    test_ascii_glyphs_match_the_legacy_atlas(fixture);
    test_dynamic_set_is_the_visible_set(fixture);
    test_rebuild_only_when_the_screen_changes(fixture);
    test_glyphs_that_left_are_dropped(fixture);
    test_frame_boundary_rule(fixture);
    test_bounded_bytes(fixture);
    test_atlas_memory_with_the_label(fixture);
    test_index_bytes_by_language(fixture);
    test_other_sizes_scale_the_one_bake(fixture);
    test_fill_guard_keeps_packing_inside_the_largest_atlas(fixture);
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
