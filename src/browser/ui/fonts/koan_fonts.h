// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_FONTS_KOAN_FONTS_H_
#define KOAN_BROWSER_UI_FONTS_KOAN_FONTS_H_

#include <string_view>

namespace koan::fonts {

// Geist Sans and Geist Mono (SIL OFL 1.1, Vercel), bundled in the binary:
// shadcn/ui's typeface. The family names Views and CSS use for them.
inline constexpr char kSansFamily[] = "Geist";
inline constexpr char kMonoFamily[] = "Geist Mono";

// One bundled TrueType face, for the native UI.
struct Face {
  std::string_view family;
  int weight;
  std::string_view data;
};

// The faces, in the order Geist Regular, Medium, SemiBold, Bold, Mono.
const Face* Faces();
int FaceCount();

// The WebUI pages' @font-face sources: Geist as one variable font (weights
// 100-900) and Geist Mono Regular, as WOFF2. (WebUI data sources give
// .woff2 files a font MIME type, and no other font format.)
std::string_view SansWoff2();
std::string_view MonoWoff2();

// Makes the faces available to the native UI by their family names, once
// (macOS: CoreText, for this process only; other platforms keep the system
// font, as Koan only ships on macOS). Returns whether they are available.
bool RegisterBundledFonts();

}  // namespace koan::fonts

#endif  // KOAN_BROWSER_UI_FONTS_KOAN_FONTS_H_
