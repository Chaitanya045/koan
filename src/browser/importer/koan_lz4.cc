// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/importer/koan_lz4.h"

#include <algorithm>
#include <iterator>

#include "base/numerics/byte_conversions.h"
#include "base/strings/string_view_util.h"

namespace koan::importer {

namespace {

// Firefox's mozLz4 header (toolkit/components/lz4/lz4.js): the magic, then
// the size.
constexpr uint8_t kMozLz4Magic[] = {'m', 'o', 'z', 'L', 'z', '4', '0', '\0'};
constexpr size_t kMozLz4HeaderSize = 12;

// More than any session file: a corrupt size never makes Koan reserve it.
constexpr uint32_t kMaxMozLz4Size = 256u << 20;

// A match copies at least this many bytes (lz4's MINMATCH).
constexpr size_t kMinMatch = 4;

}  // namespace

std::optional<std::string> DecompressLz4Block(base::span<const uint8_t> block,
                                              size_t size) {
  std::string out;
  out.reserve(size);
  size_t pos = 0;
  // A length of 15 goes on in the next bytes: each adds its value, until one
  // that isn't 255. Never more than what is left to write.
  const auto read_length = [&](size_t length) -> std::optional<size_t> {
    if (length != 15) {
      return length;
    }
    while (pos < block.size()) {
      const uint8_t byte = block[pos++];
      length += byte;
      if (length > size) {
        return std::nullopt;
      }
      if (byte != 255) {
        return length;
      }
    }
    return std::nullopt;
  };
  // Sequences: a token (literal length, match length - 4), the literals, a
  // 16-bit offset back into what is written, and the match. The last one
  // has literals only.
  while (pos < block.size()) {
    const uint8_t token = block[pos++];
    const std::optional<size_t> literals = read_length(token >> 4);
    if (!literals || *literals > block.size() - pos ||
        *literals > size - out.size()) {
      return std::nullopt;
    }
    out.append(base::as_string_view(block.subspan(pos, *literals)));
    pos += *literals;
    if (pos == block.size()) {
      break;
    }
    if (block.size() - pos < 2) {
      return std::nullopt;
    }
    const size_t offset = static_cast<size_t>(block[pos]) |
                          (static_cast<size_t>(block[pos + 1]) << 8);
    pos += 2;
    if (offset == 0 || offset > out.size()) {
      return std::nullopt;
    }
    const std::optional<size_t> match = read_length(token & 0x0f);
    if (!match || *match + kMinMatch > size - out.size()) {
      return std::nullopt;
    }
    // One byte at a time: a match may overlap what it writes (a run).
    for (size_t i = 0; i < *match + kMinMatch; ++i) {
      out.push_back(out[out.size() - offset]);
    }
  }
  if (out.size() != size) {
    return std::nullopt;
  }
  return out;
}

std::optional<std::string> ReadMozLz4(base::span<const uint8_t> file) {
  if (file.size() < kMozLz4HeaderSize ||
      !std::ranges::equal(file.first(std::size(kMozLz4Magic)), kMozLz4Magic)) {
    return std::nullopt;
  }
  const uint32_t size = base::U32FromLittleEndian(
      file.subspan(std::size(kMozLz4Magic)).first<4u>());
  if (size > kMaxMozLz4Size) {
    return std::nullopt;
  }
  return DecompressLz4Block(file.subspan(kMozLz4HeaderSize), size);
}

}  // namespace koan::importer
