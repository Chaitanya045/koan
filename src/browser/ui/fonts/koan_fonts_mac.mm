// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/fonts/koan_fonts.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>

#include "base/apple/scoped_cftyperef.h"

namespace koan::fonts {

bool RegisterBundledFonts() {
  static const bool registered = [] {
    bool all = true;
    for (const Face& face : Faces()) {
      // The provider copies nothing: the bytes live in the binary for good.
      base::apple::ScopedCFTypeRef<CGDataProviderRef> provider(
          CGDataProviderCreateWithData(nullptr, face.data.data(),
                                       face.data.size(), nullptr));
      base::apple::ScopedCFTypeRef<CGFontRef> font(
          CGFontCreateWithDataProvider(provider.get()));
      // Registered for this process only, nothing is installed.
      all &= font && CTFontManagerRegisterGraphicsFont(font.get(), nullptr);
    }
    return all;
  }();
  return registered;
}

}  // namespace koan::fonts
