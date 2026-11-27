// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/fonts/koan_fonts.h"

#include <fontconfig/fontconfig.h>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "ui/gfx/linux/fontconfig_util.h"

namespace koan::fonts {

namespace {

// Where the build puts the faces as TrueType files (//koan/browser/ui/fonts:
// font_files): next to the binary, as in the Flatpak (/app/koan).
constexpr char kFontDir[] = "koan_fonts";

}  // namespace

bool RegisterBundledFonts() {
  static const bool registered = [] {
    base::FilePath assets;
    if (!base::PathService::Get(base::DIR_ASSETS, &assets)) {
      return false;
    }
    const base::FilePath dir = assets.Append(kFontDir);
    // Adds the faces to Chromium's fontconfig configuration as application
    // fonts, for this process only: Skia looks families up in it, so Views
    // finds "Geist" by name. Nothing is installed.
    if (!FcConfigAppFontAddDir(
            gfx::GetGlobalFontConfig(),
            reinterpret_cast<const FcChar8*>(dir.value().c_str()))) {
      return false;
    }
    // A missing or empty folder isn't an error to fontconfig: check that
    // the family is there.
    FcPattern* pattern = FcPatternCreate();
    FcPatternAddString(pattern, FC_FAMILY,
                       reinterpret_cast<const FcChar8*>(kSansFamily));
    FcObjectSet* objects = FcObjectSetBuild(FC_FAMILY, nullptr);
    FcFontSet* found = FcFontList(gfx::GetGlobalFontConfig(), pattern, objects);
    const bool any = found && found->nfont > 0;
    if (found) {
      FcFontSetDestroy(found);
    }
    FcObjectSetDestroy(objects);
    FcPatternDestroy(pattern);
    return any;
  }();
  return registered;
}

}  // namespace koan::fonts
