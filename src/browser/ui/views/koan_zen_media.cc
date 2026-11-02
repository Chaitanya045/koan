// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenMediaController.mjs
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_zen_media.h"

#include <stdint.h>

#include <algorithm>
#include <cmath>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"

namespace koan {

std::u16string FormatMediaTime(base::TimeDelta time) {
  const int64_t total =
      std::max<int64_t>(0, static_cast<int64_t>(std::ceil(time.InSecondsF())));
  const int64_t hours = total / 3600;
  const int64_t minutes = total % 3600 / 60;
  const int64_t seconds = total % 60;
  const auto two_digits = [](int64_t value) {
    return (value < 10 ? u"0" : u"") + base::NumberToString16(value);
  };
  if (hours > 0) {
    return base::StrCat({base::NumberToString16(hours), u":",
                         two_digits(minutes), u":", two_digits(seconds)});
  }
  return base::StrCat(
      {base::NumberToString16(minutes), u":", two_digits(seconds)});
}

}  // namespace koan
