// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenFolder.mjs
//   ZenFolders.mjs
//   zen-folders.ftl
//   zen-workspaces.ftl
//   zen-vertical-tabs.ftl
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_folder_zen_assets.h"

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "third_party/skia/include/core/SkRect.h"

namespace koan {

void AddZenFolderBack(SkPathBuilder& path) {
  path.moveTo(8, 5.625f)
      .lineTo(11.9473f, 5.625f)
      .cubicTo(12.4866f, 5.625f, 13.0105f, 5.80861f, 13.4316f, 6.14551f)
      .lineTo(14.2881f, 6.83105f)
      .cubicTo(14.9308f, 7.34508f, 15.7298f, 7.625f, 16.5527f, 7.625f)
      .lineTo(20, 7.625f)
      .cubicTo(21.3117f, 7.625f, 22.375f, 8.68832f, 22.375f, 10)
      .lineTo(22.375f, 20)
      .cubicTo(22.375f, 21.3117f, 21.3117f, 22.375f, 20, 22.375f)
      .lineTo(8, 22.375f)
      .cubicTo(6.68832f, 22.375f, 5.625f, 21.3117f, 5.625f, 20)
      .lineTo(5.625f, 8)
      .cubicTo(5.625f, 6.68832f, 6.68832f, 5.625f, 8, 5.625f)
      .close();
}

void AddZenFolderFront(SkPathBuilder& path) {
  path.addRRect(SkRRect::MakeRectXY(
      SkRect::MakeXYWH(5.625f, 9.625f, 16.75f, 12.75f), 2.375f, 2.375f));
}

void AddZenFolderDots(SkPathBuilder& path) {
  for (float x : {10.f, 14.f, 18.f}) {
    path.addCircle(x, 16, 1.25f);
  }
}

std::u16string FormatRelativeTime(base::Time time) {
  const int64_t seconds = (base::Time::Now() - time).InSeconds();
  const auto plural = [](int64_t n, const char* unit) {
    return base::UTF8ToUTF16(base::StrCat(
        {base::NumberToString(n), " ", unit, n == 1 ? "" : "s", " ago"}));
  };
  if (seconds < 60) {
    return u"Just now";
  }
  const int64_t minutes = seconds / 60;
  if (minutes < 60) {
    return plural(minutes, "minute");
  }
  const int64_t hours = minutes / 60;
  if (hours < 24) {
    return plural(hours, "hour");
  }
  const int64_t days = hours / 24;
  if (days < 30) {
    return plural(days, "day");
  }
  return plural(days / 30, "month");
}

std::u16string FolderSearchPlaceholder(std::u16string_view folder_name) {
  return base::StrCat({u"Search ", folder_name, u"..."});
}

}  // namespace koan
