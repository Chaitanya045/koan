// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/fonts/koan_fonts.h"

#include <cstddef>
#include <string_view>

#include "koan/browser/ui/fonts/koan_fonts_data.h"

namespace koan::fonts {

namespace {

std::string_view AsChars(const unsigned char* data, size_t size) {
  return std::string_view(reinterpret_cast<const char*>(data), size);
}

}  // namespace

const Face* Faces() {
  // Not constexpr: the bytes are viewed as chars.
  static const Face faces[] = {
      {kSansFamily, 400, AsChars(kGeistRegular, kGeistRegularSize)},
      {kSansFamily, 500, AsChars(kGeistMedium, kGeistMediumSize)},
      {kSansFamily, 600, AsChars(kGeistSemiBold, kGeistSemiBoldSize)},
      {kSansFamily, 700, AsChars(kGeistBold, kGeistBoldSize)},
      {kMonoFamily, 400, AsChars(kGeistMonoRegular, kGeistMonoRegularSize)},
  };
  return faces;
}

int FaceCount() {
  return 5;
}

std::string_view SansWoff2() {
  return AsChars(kGeistVariableWoff2, kGeistVariableWoff2Size);
}

std::string_view MonoWoff2() {
  return AsChars(kGeistMonoWoff2, kGeistMonoWoff2Size);
}

}  // namespace koan::fonts
