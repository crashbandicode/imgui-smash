// Host tests of the overlay's fonts (src/imgui_backend/font_setup.cpp) through ImGui's own text path:
// ImGui::TextUnformatted in a window, as Deluxe draws its labels, with the backend's side of
// ImGuiBackendFlags_RendererHasTextures honoured after each frame. A drawn character's glyph is what
// ImFont::RenderText draws for it: baked->FindGlyph() of the codepoint ImTextCharFromUtf8 decodes.
//
// Usage: font_setup_test <host-tests/fixtures/label-fixture.ttf> <fonts/JetBrainsMonoNL-Regular.ttf>
#include "imgui_backend/font_setup.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "JetBrainsMonoNL-Regular.h"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using ImguiFontSetup::Limits;
using ImguiFontSetup::SharedFont;

int g_failures = 0;

void expect(bool condition, const char *name) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s\n", name);
    g_failures += 1;
  }
}

const char *const kLabel = "Æsir テスト日本";

std::vector<unsigned char> readFile(const char *path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

SharedFont shared(const std::vector<unsigned char> &bytes) { return {bytes.data(), bytes.size()}; }

struct Overlay {
  ImGuiContext *context;
  ImFont *font;

  explicit Overlay(std::vector<SharedFont> fonts, const Limits &limits = ImguiFontSetup::kDefaultLimits) {
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

// The backend's side of the texture protocol, as renderDrawData's updateTexture honours it.
void honourTextures() {
  static intptr_t next = 1;
  for (ImTextureData *tex : ImGui::GetPlatformIO().Textures) {
    if (tex->Status == ImTextureStatus_WantCreate) {
      tex->SetTexID(static_cast<ImTextureID>(next++));
      tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantUpdates) {
      tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantDestroy) {
      tex->SetTexID(ImTextureID_Invalid);
      tex->BackendUserData = nullptr;
      tex->SetStatus(ImTextureStatus_Destroyed);
    }
  }
}

void frame(const std::vector<std::string> &lines, float pushedSize = 0.0f) {
  ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(1200, 700));
  ImGui::Begin("Opp");
  if (pushedSize > 0.0f) {
    ImGui::PushFont(nullptr, pushedSize);
  }
  for (const std::string &line : lines) {
    ImGui::TextUnformatted(line.c_str());
  }
  if (pushedSize > 0.0f) {
    ImGui::PopFont();
  }
  ImGui::End();
  ImGui::Render();
  honourTextures();
}

// (codepoint decoded, codepoint of the glyph drawn for it) for each character of `utf8`.
std::vector<std::pair<unsigned, unsigned>> drawn(ImFont *font, const char *utf8) {
  ImFontBaked *baked = font->GetFontBaked(ImguiFontSetup::kFontSize);
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

std::string kanji(int first, int count) {
  std::string out;
  for (int i = 0; i < count; ++i) {
    char buf[5] = {};
    ImTextCharToUtf8(buf, static_cast<unsigned>(0x4E00 + first + i));
    out += buf;
  }
  return out;
}

long long livePacked() {
  const ImFontAtlasBuilder *builder = ImGui::GetIO().Fonts->Builder;
  return static_cast<long long>(builder->RectsPackedSurface) - builder->RectsDiscardedSurface;
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
  frame({kLabel, "æ Ø é"});
  int fallback = 0;
  for (auto [c, glyph] : drawn(overlay.font, kLabel)) {
    if (c >= 0x80) {
      fallback += glyph == '?' ? 1 : 0;
    } else {
      expect(c == glyph, "ASCII draws itself with the main font alone");
    }
  }
  expect(fallback == 6, "Æ and the five kana/kanji each draw '?' without a shared font");
}

void test_shared_font_draws_each_character_of_the_name(const std::vector<unsigned char> &fixture) {
  Overlay overlay({shared(fixture)});
  frame({kLabel, "æ Ø é Å ø", "日本語のなまえ", "한글"});
  expect(allOwn(overlay.font, kLabel), "Æsir テスト日本 draws its own glyphs");
  expect(allOwn(overlay.font, "æØéÅø"), "Latin-1 draws its own glyphs");
  expect(allOwn(overlay.font, "日本語のなまえ"), "kana and kanji draw their own glyphs");
  expect(allOwn(overlay.font, "한글"), "Hangul draws its own glyphs");
  ImFontBaked *baked = overlay.font->GetFontBaked(ImguiFontSetup::kFontSize);
  expect(baked->FindGlyph('s')->SourceIdx == 0, "ASCII still comes from JetBrains Mono");
  expect(baked->FindGlyph(0x30C6)->SourceIdx == 1, "kana comes from the shared font");
  // A character no source has costs that one character.
  frame({"A\xE0\xB8\x81" "B"});
  const auto thai = drawn(overlay.font, "A\xE0\xB8\x81" "B");
  expect(thai.size() == 3 && thai[0].second == 'A' && thai[2].second == 'B', "the characters around it draw");
  expect(thai.size() == 3 && thai[1].second == '?', "U+0E01 draws the fallback glyph");
  const ImguiFontSetup::Stats stats = ImguiFontSetup::stats();
  expect(stats.sharedSources == 1 && stats.rejectedSources == 0, "one shared font merged");
  expect(stats.admitted == 18, "each of the 18 distinct non-ASCII characters was rasterized once");
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
  ImFontBaked *baked = overlay.font->GetFontBaked(ImguiFontSetup::kFontSize);
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

void test_atlas_memory_with_the_label(const std::vector<unsigned char> &fixture) {
  Overlay overlay({shared(fixture)});
  frame({kLabel, "日本語のなまえ", "Opp: LLUltra | 4 (12ms)"});
  ImTextureData *tex = ImGui::GetIO().Fonts->TexData;
  const int bytes = tex->GetSizeInBytes();
  const int legacy = 512 * 128 * 4;
  std::printf("memory: legacy atlas 512x128 RGBA32 %d B; new atlas %dx%d bpp %d %d B; glyphs admitted %d; "
              "live packed %lld px\n",
              legacy, tex->Width, tex->Height, tex->BytesPerPixel, bytes, ImguiFontSetup::stats().admitted,
              livePacked());
  expect(tex->Format == ImTextureFormat_Alpha8, "the atlas is Alpha8");
  expect(bytes * 4 <= legacy, "the label's atlas is at most a quarter of the legacy one");
}

void test_other_sizes_scale_the_one_bake(const std::vector<unsigned char> &fixture) {
  Overlay overlay({shared(fixture)});
  frame({kLabel});
  frame({kLabel}, 13.5f);
  frame({kLabel}, 22.0f);
  expect(ImGui::GetIO().Fonts->Builder->BakedPool.Size == 1, "the compact strip's sizes add no bake");
  expect(overlay.font->GetFontBaked(13.5f)->Size == ImguiFontSetup::kFontSize, "13.5 px scales the 18 px bake");
}

void test_glyph_budget_refuses_past_the_limit(const std::vector<unsigned char> &fixture) {
  Limits limits = ImguiFontSetup::kDefaultLimits;
  limits.glyphBudget = 10;
  Overlay overlay({shared(fixture)}, limits);
  const std::string forty = kanji(0, 40);
  frame({forty});
  const ImguiFontSetup::Stats stats = ImguiFontSetup::stats();
  int own = 0;
  for (auto [c, glyph] : drawn(overlay.font, forty.c_str())) {
    own += c == glyph ? 1 : 0;
  }
  expect(stats.admitted == 10, "ten shared-font glyphs admitted");
  expect(own == 10, "the first ten kanji draw themselves");
  expect(stats.refusals >= 30, "the other thirty were refused");
}

// The fill guard keeps packing below the largest atlas; ImGui's "Out of texture memory" assert never fires.
void test_fill_guard_keeps_packing_inside_the_largest_atlas(const std::vector<unsigned char> &fixture) {
  const Limits limits = {1000, 0.5f, 256, 128, 256, 256};
  Overlay overlay({shared(fixture)}, limits);
  frame({"The quick brown fox jumps over the lazy dog 0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"});
  const long long ascii = livePacked();
  for (int batch = 0; batch < 8; ++batch) {
    frame({kanji(batch * 20, 20)});
  }
  const ImguiFontSetup::Stats stats = ImguiFontSetup::stats();
  ImTextureData *tex = ImGui::GetIO().Fonts->TexData;
  std::printf("fill guard: ascii packed %lld px, final packed %lld px of %d, admitted %d, refusals %d, atlas %dx%d\n",
              ascii, livePacked(), 256 * 256, stats.admitted, stats.refusals, tex->Width, tex->Height);
  expect(stats.admitted > 0 && stats.refusals > 0, "some kanji admitted, the rest refused");
  expect(livePacked() <= 256 * 256 / 2, "packing stays at or below half of the largest atlas");
  expect(tex->Width <= 256 && tex->Height <= 256, "the atlas never exceeds its maximum");
}

// 160 kanji into an atlas capped at 256x128, in a child process. Returns its wait status.
int overfill(const std::vector<unsigned char> &fixture, float fillLimit) {
  std::fflush(stdout);
  std::fflush(stderr);
  const pid_t child = fork();
  if (child == 0) {
    const Limits limits = {1000, fillLimit, 256, 128, 256, 128};
    Overlay overlay({shared(fixture)}, limits);
    for (int batch = 0; batch < 8; ++batch) {
      frame({kanji(batch * 20, 20)});
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

}  // namespace

int main(int argc, char **argv) {
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
  test_validation_accepts_fonts_and_rejects_anything_else(fixture, jetbrains);
  test_main_font_alone_draws_question_marks_for_the_name();
  test_shared_font_draws_each_character_of_the_name(fixture);
  test_fallback_prefers_the_replacement_glyph(fixture, jetbrains);
  test_ascii_glyphs_match_the_legacy_atlas(fixture);
  test_atlas_memory_with_the_label(fixture);
  test_other_sizes_scale_the_one_bake(fixture);
  test_glyph_budget_refuses_past_the_limit(fixture);
  test_fill_guard_keeps_packing_inside_the_largest_atlas(fixture);
  test_without_the_fill_guard_imgui_aborts(fixture);
  if (g_failures == 0) {
    std::printf("font_setup_test: ok\n");
  }
  return g_failures == 0 ? 0 : 1;
}
