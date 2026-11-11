// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/vector_icons/koan_icons.h"

#include <algorithm>
#include <set>
#include <string>

#include "chrome/app/vector_icons/vector_icons.h"
#include "components/vector_icons/vector_icons.h"
#include "koan/browser/ui/vector_icons/koan_icon_sizes.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/image/image_unittest_util.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/vector_icons.h"

namespace koan {

namespace {

// The canvas every Koan icon is drawn on (scripts/svg_to_icon.py).
constexpr float kCanvas = 24;

bool IsKoanIcon(const gfx::VectorIcon& icon) {
  return std::string(icon.name).starts_with("kKoan");
}

}  // namespace

TEST(KoanIconsTest, TheTableIsNotEmpty) {
  EXPECT_GT(KoanIconTable().size(), 250u);
}

TEST(KoanIconsTest, EveryEntryExists) {
  for (const KoanIconEntry& entry : KoanIconTable()) {
    ASSERT_TRUE(entry.chromium);
    ASSERT_TRUE(entry.koan);
    EXPECT_FALSE(entry.chromium->is_empty()) << entry.chromium->name;
    EXPECT_FALSE(entry.koan->is_empty()) << entry.koan->name;
    // A Chromium icon is swapped for a Koan one, never for another Chromium
    // icon, and not for itself.
    EXPECT_TRUE(IsKoanIcon(*entry.koan)) << entry.koan->name;
    EXPECT_FALSE(IsKoanIcon(*entry.chromium)) << entry.chromium->name;
  }
}

TEST(KoanIconsTest, KoanIconsAreOneRepOnTheKoanCanvas) {
  for (const KoanIconEntry& entry : KoanIconTable()) {
    ASSERT_EQ(entry.koan->reps.size(), 1u) << entry.koan->name;
    const base::span<const gfx::PathElement> path = entry.koan->reps[0].path;
    ASSERT_GT(path.size(), 4u) << entry.koan->name;
    EXPECT_EQ(path[0].command, gfx::CANVAS_DIMENSIONS) << entry.koan->name;
    EXPECT_EQ(path[1].arg, kCanvas) << entry.koan->name;
  }
}

TEST(KoanIconsTest, EveryChromiumIconIsMappedOnce) {
  std::set<const gfx::VectorIcon*> seen;
  for (const KoanIconEntry& entry : KoanIconTable()) {
    EXPECT_TRUE(seen.insert(entry.chromium).second) << entry.chromium->name;
  }
}

TEST(KoanIconsTest, ChromiumIconsAreSwappedForTheirKoanIcon) {
  for (const KoanIconEntry& entry : KoanIconTable()) {
    EXPECT_EQ(&KoanIconFor(*entry.chromium), entry.koan)
        << entry.chromium->name;
  }
  // Spot checks of the surfaces the e2e test covers.
  EXPECT_EQ(&KoanIconFor(vector_icons::kBackArrowChromeRefreshOldIcon),
            &kKoanBackIcon);
  EXPECT_EQ(&KoanIconFor(vector_icons::kForwardArrowChromeRefreshOldIcon),
            &kKoanForwardIcon);
  EXPECT_EQ(&KoanIconFor(vector_icons::kReloadChromeRefreshOldIcon),
            &kKoanReloadIcon);
  EXPECT_EQ(&KoanIconFor(vector_icons::kAdd2Icon), &kKoanPlusIcon);
  EXPECT_EQ(&KoanIconFor(vector_icons::kCloseIcon), &kKoanCloseIcon);
  // The media card's picture-in-picture button.
  EXPECT_EQ(&KoanIconFor(vector_icons::kPictureInPictureAltIcon),
            &kKoanPictureInPictureIcon);
  // Zen's unpin: a dash (the reset button of a pinned tab).
  EXPECT_EQ(&KoanIconFor(kRemoveIcon), &kKoanUnpinIcon);
  // The sidebar toggle, expanded and collapsed.
  EXPECT_EQ(&KoanIconFor(views::kMenuCloseCustomIcon), &kKoanSidebarIcon);
  EXPECT_EQ(&KoanIconFor(views::kMenuOpenIcon), &kKoanSidebarIcon);
  EXPECT_EQ(&KoanIconFor(views::kMenuOpenOldIcon), &kKoanSidebarIcon);
}

TEST(KoanIconsTest, KoanIconsAndUnmappedIconsAreLeftAlone) {
  EXPECT_EQ(&KoanIconFor(kKoanBackIcon), &kKoanBackIcon);
  EXPECT_EQ(&KoanIconFor(kKoanLibraryIcon), &kKoanLibraryIcon);
  // Nothing in Zen's vocabulary: Chromium's own.
  EXPECT_EQ(&KoanIconFor(vector_icons::kCloudOffIcon),
            &vector_icons::kCloudOffIcon);
}

SkBitmap Rendered(const gfx::ImageSkia& image) {
  return image.GetRepresentation(1.0f).GetBitmap();
}

// The icon painted at `size` x `size` (a square bitmap).
SkBitmap Painted(const gfx::VectorIcon& icon, int size) {
  gfx::Canvas canvas(gfx::Size(size, size), 1.0f, /*is_opaque=*/false);
  gfx::PaintVectorIcon(&canvas, icon, size, SK_ColorBLACK);
  return canvas.GetBitmap();
}

// What the patch to ui/gfx/paint_vector_icon.cc (docs/REBASE.md) must keep
// doing: both ways of drawing an icon ask the registry. Fails if a rebase
// drops the hook, or the icon cache keys something else than the icon drawn.
TEST(KoanIconsHookTest, ChromiumIconsPaintAsKoanIcons) {
  InstallVectorIconOverride();
  constexpr int kSize = 17;
  const gfx::VectorIcon& chromium =
      vector_icons::kBackArrowChromeRefreshOldIcon;

  // gfx::CreateVectorIcon, as every ImageModel and image button does.
  const SkBitmap created =
      Rendered(gfx::CreateVectorIcon(chromium, kSize, SK_ColorBLACK));
  EXPECT_TRUE(gfx::test::AreBitmapsEqual(
      created,
      Rendered(gfx::CreateVectorIcon(kKoanBackIcon, kSize, SK_ColorBLACK))));
  // gfx::PaintVectorIcon, as views that draw in OnPaint do.
  EXPECT_TRUE(gfx::test::AreBitmapsEqual(Painted(chromium, kSize),
                                         Painted(kKoanBackIcon, kSize)));

  // The comparison tells icons apart: Back is not Forward.
  EXPECT_FALSE(gfx::test::AreBitmapsEqual(
      created,
      Rendered(gfx::CreateVectorIcon(kKoanForwardIcon, kSize, SK_ColorBLACK))));
  // And something was drawn.
  bool drew = false;
  for (int y = 0; y < created.height(); ++y) {
    for (int x = 0; x < created.width(); ++x) {
      drew |= SkColorGetA(created.getColor(x, y)) != 0;
    }
  }
  EXPECT_TRUE(drew);
}

// The magnifier of the URL bar is Firefox's own glyph (Zen's identity icon):
// a 16 unit box, half opaque, with 14 units of ink (the 13.5 pt measured in
// #178 is its anti-aliased edge).
TEST(KoanIconsTest, UrlBarMagnifierIsFirefoxsAtHalfOpacity) {
  const base::span<const gfx::PathElement> path =
      kKoanUrlbarSearchGlassIcon.reps[0].path;
  ASSERT_GT(path.size(), 5u);
  EXPECT_EQ(path[0].command, gfx::CANVAS_DIMENSIONS);
  EXPECT_EQ(path[1].arg, 16.f);
  EXPECT_EQ(path[2].command, gfx::FILL_RULE_NONZERO);
  EXPECT_EQ(path[3].command, gfx::PATH_COLOR_ALPHA);
  EXPECT_EQ(path[4].arg, 128.f);

  // Ink at 10 pixels to the unit.
  constexpr int kScale = 10;
  const SkBitmap bitmap = Painted(kKoanUrlbarSearchGlassIcon, 16 * kScale);
  int left = bitmap.width(), top = bitmap.height(), right = -1, bottom = -1;
  for (int y = 0; y < bitmap.height(); ++y) {
    for (int x = 0; x < bitmap.width(); ++x) {
      if (SkColorGetA(bitmap.getColor(x, y)) > 16) {
        left = std::min(left, x);
        right = std::max(right, x + 1);
        top = std::min(top, y);
        bottom = std::max(bottom, y + 1);
      }
    }
  }
  ASSERT_GT(right, left);
  EXPECT_NEAR((right - left) / static_cast<double>(kScale), 14.0, 0.25);
  EXPECT_NEAR((bottom - top) / static_cast<double>(kScale), 14.0, 0.25);
  // Half opaque where it is drawn.
  EXPECT_NEAR(SkColorGetA(bitmap.getColor(15, 65)), 128, 2);
}

// Zen's icon sizes (.agent/notes/zen-icons-inventory.md, "Render size").
TEST(KoanIconsTest, SizesAreZens) {
  EXPECT_EQ(kToolbarIconSize, 17);
  EXPECT_EQ(kFaviconIconSize, 16);
  EXPECT_EQ(kMenuIconSize, 18);
  EXPECT_EQ(kCreateIconSize, 20);
}

}  // namespace koan
