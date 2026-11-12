// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_IMPORTER_KOAN_LZ4_H_
#define KOAN_BROWSER_IMPORTER_KOAN_LZ4_H_

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>

#include "base/containers/span.h"

// Reading Firefox's (and so Zen's) ".jsonlz4" files (#363), for the import of
// Zen's Spaces: mozLz4, an 8-byte magic "mozLz40\0", the uncompressed size
// (32 bits, little-endian) and one LZ4 block (lz4's block format, no frame).
// Chromium has no LZ4 library, so this is a small decoder of that format.
// Pure: unit-tested in //koan/test:koan_unittests.
namespace koan::importer {

// The `size` bytes an LZ4 block decompresses to, or nullopt if `block` is
// malformed or doesn't decompress to exactly `size` bytes.
std::optional<std::string> DecompressLz4Block(base::span<const uint8_t> block,
                                              size_t size);

// The contents of a mozLz4 file, or nullopt if it isn't one, is malformed, or
// is larger than Koan reads (256 MiB uncompressed).
std::optional<std::string> ReadMozLz4(base::span<const uint8_t> file);

}  // namespace koan::importer

#endif  // KOAN_BROWSER_IMPORTER_KOAN_LZ4_H_
