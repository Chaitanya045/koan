// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/importer/koan_lz4.h"

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::importer {

namespace {

std::vector<uint8_t> Bytes(std::string_view hex) {
  std::vector<uint8_t> bytes;
  EXPECT_TRUE(base::HexStringToBytes(hex, &bytes)) << hex;
  return bytes;
}

// Compressed by the reference lz4 (python-lz4 4.4, lz4.block.compress(...,
// store_size=False)): literals, matches, and a run (a match one byte back
// that overlaps what it writes).
constexpr char kJson[] = R"({"name":"Zen","name2":"Zen","pad":")"
                         "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                         R"("})";
constexpr char kJsonLz4[] =
    "e17b226e616d65223a225a656e222c0d0015320e007f706164223a227801001150787878"
    "227d";

// Lengths of 15 and more go on in extra bytes: 26 literals, then a 300-byte
// run and a 23-byte match.
constexpr char kLongLz4[] =
    "ff0c6162636465666768696a6b6c6d6e6f707172737475767778797a2d0100ff190f4601"
    "03507778797a21";

std::string Long() {
  return std::string("abcdefghijklmnopqrstuvwxyz") + std::string(300, '-') +
         "abcdefghijklmnopqrstuvwxyz!";
}

}  // namespace

TEST(KoanLz4Test, DecompressesBlocks) {
  const std::string json = kJson;
  EXPECT_EQ(json, DecompressLz4Block(Bytes(kJsonLz4), json.size()));
  EXPECT_EQ(Long(), DecompressLz4Block(Bytes(kLongLz4), Long().size()));
  // Nothing: a block of one empty literal run.
  EXPECT_EQ(std::string(), DecompressLz4Block(Bytes("00"), 0));
  EXPECT_EQ(std::string(), DecompressLz4Block({}, 0));
}

TEST(KoanLz4Test, RejectsMalformedBlocks) {
  const std::string json = kJson;
  const std::vector<uint8_t> block = Bytes(kJsonLz4);
  // Another size than the block's.
  EXPECT_FALSE(DecompressLz4Block(block, json.size() - 1));
  EXPECT_FALSE(DecompressLz4Block(block, json.size() + 1));
  // Cut short: in the literals, in an offset, in a length.
  for (size_t size : {size_t{3}, size_t{16}, size_t{17}, block.size() - 3}) {
    EXPECT_FALSE(DecompressLz4Block(base::span(block).first(size), json.size()))
        << size;
  }
  // A match before the start, or of offset 0.
  EXPECT_FALSE(DecompressLz4Block(Bytes("1061050000"), 5));
  EXPECT_FALSE(DecompressLz4Block(Bytes("1061000000"), 5));
  // A length that never ends.
  EXPECT_FALSE(DecompressLz4Block(Bytes("f0ffffff"), 1000));
}

TEST(KoanLz4Test, ReadsMozLz4) {
  // Firefox's (Zen's) .jsonlz4: the magic, the size, the block.
  const std::vector<uint8_t> file = Bytes(
      "6d6f7a4c7a3430003a000000f02b7b22737061636573223a205b7b2275756964223a"
      "20227b73317d222c20226e616d65223a2022576f726b227d5d2c202274616273223a"
      "205b5d7d");
  EXPECT_EQ(R"({"spaces": [{"uuid": "{s1}", "name": "Work"}], "tabs": []})",
            ReadMozLz4(file));

  // Not a mozLz4 file, or too short for one.
  std::vector<uint8_t> other = file;
  other[0] = 'n';
  EXPECT_FALSE(ReadMozLz4(other));
  EXPECT_FALSE(ReadMozLz4(base::span(file).first(11u)));
  // A size that isn't the block's.
  std::vector<uint8_t> wrong_size = file;
  wrong_size[8] = 0x3b;
  EXPECT_FALSE(ReadMozLz4(wrong_size));
  // A size past what Koan reads is never reserved.
  std::vector<uint8_t> huge = file;
  huge[11] = 0x7f;
  EXPECT_FALSE(ReadMozLz4(huge));
}

}  // namespace koan::importer
