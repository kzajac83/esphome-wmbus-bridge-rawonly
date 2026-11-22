// SPDX-License-Identifier: GPL-3.0-or-later
// Host test for the stream-capture length helpers in frame_length.h, in
// particular expected_raw_len_c1_or_t1(), which the SX1262 streaming path uses
// for listen_mode c1 and both.
//
// Build (see .github/workflows/ci.yml): g++ -std=c++17 with -Itests/host and
// -Icomponents/wmbus_radio, plus components/wmbus_radio/decode3of6.cpp.

#include "frame_length.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace esphome::wmbus_radio;

static int failures = 0;

#define CHECK_EQ(actual, expected)                                                              \
  do {                                                                                          \
    const auto a_ = (actual);                                                                   \
    const auto e_ = (expected);                                                                 \
    if (a_ != e_) {                                                                             \
      std::printf("FAIL %s:%d: %s = %ld, expected %ld\n", __FILE__, __LINE__, #actual, (long) a_, \
                  (long) e_);                                                                   \
      failures++;                                                                               \
    }                                                                                           \
  } while (0)

// 3-of-6 code words, EN 13757-4 table 10, nibble 0..F.
static const uint8_t kCode[16] = {0x16, 0x0D, 0x0E, 0x0B, 0x1C, 0x19, 0x1A, 0x13,
                                  0x2C, 0x25, 0x26, 0x23, 0x34, 0x31, 0x32, 0x29};

static std::vector<uint8_t> encode3of6(const std::vector<uint8_t> &in) {
  std::vector<uint8_t> out;
  uint32_t acc = 0;
  int bits = 0;
  for (uint8_t b : in) {
    for (uint8_t nib : {(uint8_t) (b >> 4), (uint8_t) (b & 0x0F)}) {
      acc = (acc << 6) | kCode[nib];
      bits += 6;
      while (bits >= 8) {
        out.push_back((uint8_t) (acc >> (bits - 8)));
        bits -= 8;
      }
    }
  }
  if (bits > 0)
    out.push_back((uint8_t) (acc << (8 - bits)));
  return out;
}

static size_t blocks_a(uint8_t l) { return (l < 26) ? 2 : (size_t) ((l - 26) / 16 + 3); }

int main() {
  // --- C1 format B, the heat meter from the field report ---------------------
  // The published (normalised, CRC-stripped) telegram shows L=0xA4; on air the
  // format-B L-field also counts the two block CRCs, so it is 0xA8.
  {
    std::vector<uint8_t> raw = {0x54, 0x3D, 0xA8, 0x44, 0xC5, 0x14};
    const size_t want = 2 + 1 + 0xA8;  // indicator + L + L bytes (CRCs inside L)
    CHECK_EQ(expected_raw_len_c1_or_t1(raw, true), want);
    CHECK_EQ(want, (size_t) 171);  // matches the logged raw=171
    CHECK_EQ(expected_raw_len_c1_or_t1(raw, false), want);
    CHECK_EQ(raw_len_undecidable_c1_or_t1(raw, true), false);
    // Below 3 bytes the length is not known yet, and that is not "never".
    std::vector<uint8_t> two(raw.begin(), raw.begin() + 2);
    CHECK_EQ(expected_raw_len_c1_or_t1(two, true), (size_t) 0);
    CHECK_EQ(raw_len_undecidable_c1_or_t1(two, true), false);
  }

  // --- C1 format A, smoke detector from the field log: L=0x5E -> raw 111 -----
  {
    std::vector<uint8_t> raw = {0x54, 0xCD, 0x5E, 0x44};
    CHECK_EQ(expected_raw_len_c1_or_t1(raw, true), (size_t) (2 + 0x5F + 2 * blocks_a(0x5E)));
    CHECK_EQ(expected_raw_len_c1_or_t1(raw, true), (size_t) 111);  // matches the logged raw=111
  }

  // --- C1 short format B (ITW HCA): on-air L=0x30 -> raw 51 ------------------
  {
    std::vector<uint8_t> raw = {0x54, 0x3D, 0x30};
    CHECK_EQ(expected_raw_len_c1_or_t1(raw, true), (size_t) 51);  // matches the logged raw=51
  }

  // --- T1, the FAW long telegram: L=0x9B (156 decoded -> 267 raw logged) -----
  {
    std::vector<uint8_t> dec = {0x9B, 0x44, 0xC5, 0x14};
    const auto raw = encode3of6(dec);
    CHECK_EQ(raw[0] != 0x54, true);
    const size_t want = encoded_size(0x9B + 1 + 2 * blocks_a(0x9B));
    CHECK_EQ(expected_raw_len_c1_or_t1(raw, true), want);
    CHECK_EQ(want, (size_t) 267);  // matches the logged raw=267
    // listen_mode c1 must not accept a T1 capture.
    CHECK_EQ(expected_raw_len_c1_or_t1(raw, false), (size_t) 0);
    CHECK_EQ(raw_len_undecidable_c1_or_t1(raw, false), true);
    // Equivalent to the T1-only helper for listen_mode t1.
    CHECK_EQ(expected_raw_len_c1_or_t1(raw, true), expected_raw_len_t1(raw));
  }

  // --- 0x54 can never start a valid T1 frame ---------------------------------
  {
    bool any = false;
    for (int b0 = 0; b0 < 256; b0++)
      for (int b1 = 0; b1 < 256; b1++) {
        std::vector<uint8_t> raw = {(uint8_t) b0, (uint8_t) b1};
        if (expected_raw_len_t1(raw) != 0 && b0 == 0x54)
          any = true;
      }
    CHECK_EQ(any, false);
  }

  // --- Noise: undecidable as soon as the header bytes are in -----------------
  {
    std::vector<uint8_t> bad_c = {0x54, 0x77, 0x40};  // bad C-mode format byte
    CHECK_EQ(expected_raw_len_c1_or_t1(bad_c, true), (size_t) 0);
    CHECK_EQ(raw_len_undecidable_c1_or_t1(bad_c, true), true);

    std::vector<uint8_t> bad_t = {0xFF, 0xFF};  // not 3-of-6
    CHECK_EQ(expected_raw_len_c1_or_t1(bad_t, true), (size_t) 0);
    CHECK_EQ(raw_len_undecidable_c1_or_t1(bad_t, true), true);

    std::vector<uint8_t> tiny_l = {0x54, 0x3D, 0x05};  // L too small for a frame
    CHECK_EQ(raw_len_undecidable_c1_or_t1(tiny_l, true), true);

    std::vector<uint8_t> empty;
    CHECK_EQ(raw_len_undecidable_c1_or_t1(empty, true), false);
  }

  // --- Shortest link layers: L=9 (header only) and L=10 (header + CI) --------
  // Must size exactly as Packet::expected_size() does, block 1 alone for L=9.
  {
    CHECK_EQ(format_a_block_count(9), (size_t) 1);
    CHECK_EQ(format_a_block_count(10), (size_t) 2);
    CHECK_EQ(format_a_block_count(25), (size_t) 2);
    CHECK_EQ(format_a_block_count(26), (size_t) 3);
    CHECK_EQ(format_a_block_count(0x9B), blocks_a(0x9B));

    const auto t1_l9 = encode3of6({0x09, 0x00});
    CHECK_EQ(expected_raw_len_t1(t1_l9), encoded_size(12));
    CHECK_EQ(expected_raw_len_t1(t1_l9), (size_t) 18);
    const auto t1_l10 = encode3of6({0x0A, 0x40});
    CHECK_EQ(expected_raw_len_t1(t1_l10), encoded_size(15));
    const auto t1_l8 = encode3of6({0x08, 0x00});
    CHECK_EQ(expected_raw_len_t1(t1_l8), (size_t) 0);
    CHECK_EQ(raw_len_undecidable_c1_or_t1(t1_l8, true), true);

    std::vector<uint8_t> c1a_l9 = {0x54, 0xCD, 0x09};
    CHECK_EQ(expected_raw_len_c1_or_t1(c1a_l9, true), (size_t) (2 + 10 + 2));
    // Format B keeps its bound: L counts the CRC, so L=10 is no frame.
    std::vector<uint8_t> c1b_l10 = {0x54, 0x3D, 0x0A};
    CHECK_EQ(expected_raw_len_c1_or_t1(c1b_l10, true), (size_t) 0);
    std::vector<uint8_t> c1b_l11 = {0x54, 0x3D, 0x0B};
    CHECK_EQ(expected_raw_len_c1_or_t1(c1b_l11, true), (size_t) (2 + 1 + 0x0B));

    // S1: Manchester 10 = 1, 01 = 0 (polarity false). L=9, C=0x44.
    std::vector<uint8_t> s1;
    for (uint8_t byte : {(uint8_t) 0x09, (uint8_t) 0x44}) {
      uint16_t chips = 0;
      for (int bit = 7; bit >= 0; bit--)
        chips = (uint16_t) ((chips << 2) | ((((byte >> bit) & 1) != 0) ? 0x2 : 0x1));
      s1.push_back((uint8_t) (chips >> 8));
      s1.push_back((uint8_t) (chips & 0xFF));
    }
    CHECK_EQ(expected_raw_len_s1(s1), (size_t) ((10 + 2) * 2));
  }

  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("frame_length: all checks passed\n");
  return EXIT_SUCCESS;
}
