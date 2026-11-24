#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace FontFixture {

inline unsigned read16(const unsigned char *p) { return (static_cast<unsigned>(p[0]) << 8) | p[1]; }
inline uint32_t read32(const unsigned char *p) {
  return (static_cast<uint32_t>(read16(p)) << 16) | read16(p + 2);
}

inline std::vector<unsigned char> withoutReplacement(const std::vector<unsigned char> &fixture) {
  if (fixture.size() < 12) {
    return {};
  }
  std::vector<unsigned char> font = fixture;
  const size_t tableCount = read16(font.data() + 4);
  size_t cmap = 0;
  for (size_t i = 0; i < tableCount; ++i) {
    const size_t record = 12 + i * 16;
    if (font[record] == 'c' && font[record + 1] == 'm' && font[record + 2] == 'a' && font[record + 3] == 'p') {
      cmap = read32(font.data() + record + 8);
      break;
    }
  }
  if (cmap == 0) {
    return {};
  }
  bool patched = false;
  const unsigned tables = read16(font.data() + cmap + 2);
  for (unsigned t = 0; t < tables; ++t) {
    const size_t sub = cmap + read32(font.data() + cmap + 8 + t * 8);
    if (read16(font.data() + sub) != 4) {
      continue;
    }
    const unsigned count = read16(font.data() + sub + 6) / 2;
    const size_t ends = sub + 14, starts = ends + 2 * count + 2;
    const size_t deltas = starts + 2 * count, ranges = deltas + 2 * count;
    for (unsigned i = 0; i < count; ++i) {
      if (read16(font.data() + ends + 2 * i) == 0xFFFD &&
          read16(font.data() + starts + 2 * i) == 0xFFFD &&
          read16(font.data() + ranges + 2 * i) == 0) {
        font[deltas + 2 * i] = 0;
        font[deltas + 2 * i + 1] = 3;
        patched = true;
      }
    }
  }
  return patched ? font : std::vector<unsigned char>{};
}

}  // namespace FontFixture
