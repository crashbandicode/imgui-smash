#include "helpers/nav_toggle.hpp"

#include <cstdio>

static int g_failures = 0;

static void expect(bool condition, const char *name) {
  if (!condition) {
    std::fprintf(stderr, "fail %s\n", name);
    g_failures += 1;
  }
}

static void testChordNeedsOneHeldAndTheOtherPressed() {
  expect(nav_toggle::chord(true, true, false, false), "hold L3, press R3");
  expect(nav_toggle::chord(false, false, true, true), "hold R3, press L3");
  expect(!nav_toggle::chord(true, false, false, false), "L3 alone");
  expect(!nav_toggle::chord(false, false, true, false), "R3 alone");
  expect(!nav_toggle::chord(false, true, false, true), "both pressed, neither held");
}

static void testDisabledByDefaultNeverTurnsOn() {
  bool nav = false;
  for (int frame = 0; frame < 8; ++frame) {
    nav = nav_toggle::next(nav, false, true);
    expect(!nav, "a disabled toggle ignores the chord");
  }
}

static void testDisablingTurnsNavigationOff() {
  expect(!nav_toggle::next(true, false, false), "disabled forces navigation off");
  expect(!nav_toggle::next(true, false, true), "disabled with the chord stays off");
}

static void testEnabledChordFlipsOncePerPress() {
  bool nav = false;
  nav = nav_toggle::next(nav, true, true);
  expect(nav, "first chord turns navigation on");
  nav = nav_toggle::next(nav, true, false);
  expect(nav, "no chord keeps it on");
  nav = nav_toggle::next(nav, true, true);
  expect(!nav, "second chord turns it off");
  nav = nav_toggle::next(nav, true, false);
  expect(!nav, "no chord keeps it off");
}

int main() {
  testChordNeedsOneHeldAndTheOtherPressed();
  testDisabledByDefaultNeverTurnsOn();
  testDisablingTurnsNavigationOff();
  testEnabledChordFlipsOncePerPress();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d nav toggle checks failed\n", g_failures);
    return 1;
  }
  std::printf("nav toggle checks passed\n");
  return 0;
}
