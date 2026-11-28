// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <string>

#include "koan/browser/ui/fonts/koan_fonts.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"

namespace koan::fonts {
namespace {

// The build's koan_fonts/ folder next to the test binary is registered with
// fontconfig, and Skia then finds Geist by name.
TEST(KoanFontsLinuxTest, RegistersGeist) {
  ASSERT_TRUE(RegisterBundledFonts());
  EXPECT_EQ(gfx::Font(kSansFamily, 13).GetActualFontName(), kSansFamily);
  EXPECT_EQ(gfx::Font(kMonoFamily, 13).GetActualFontName(), kMonoFamily);
}

// Upstream behaviour ApplyDefaultFont() (koan_look_controller.cc) depends on:
// ResourceBundle::ReloadFonts() resets the default font description, so Koan
// sets Geist after it. If this fails, check the order there.
TEST(KoanFontsLinuxTest, ReloadFontsResetsTheDefaultDescription) {
  ASSERT_TRUE(RegisterBundledFonts());
  ui::ResourceBundle bundle(nullptr);

  // An unusual size, which the toolkit's default font won't have.
  gfx::FontList::SetDefaultFontDescription(std::string(kSansFamily) + ", 17px");
  bundle.ReloadFonts();
  EXPECT_NE(gfx::FontList().GetFontSize(), 17);

  bundle.ReloadFonts();
  gfx::FontList::SetDefaultFontDescription(std::string(kSansFamily) + ", 17px");
  EXPECT_EQ(gfx::FontList().GetFontSize(), 17);
  EXPECT_EQ(gfx::FontList().GetPrimaryFont().GetActualFontName(), kSansFamily);

  gfx::FontList::SetDefaultFontDescription(std::string());
}

}  // namespace
}  // namespace koan::fonts
