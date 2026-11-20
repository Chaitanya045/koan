// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/version/koan_version.h"

#include "base/strings/string_util.h"
#include "components/version_info/version_info.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::version {
namespace {

TEST(KoanVersionTest, FormatsWithAndWithoutALabel) {
  EXPECT_EQ(FormatDisplayVersion("0.0.2", "Alpha", "158.0.8127.18"),
            "0.0.2 (Alpha) · Chromium 158.0.8127.18");
  EXPECT_EQ(FormatDisplayVersion("0.1.0", "Beta 2", "159.0.8200.5"),
            "0.1.0 (Beta 2) · Chromium 159.0.8200.5");
  EXPECT_EQ(FormatDisplayVersion("1.0.0", "", "160.0.8300.1"),
            "1.0.0 · Chromium 160.0.8300.1");
}

// The build wires VERSION in, and Chromium's version stays the real one.
TEST(KoanVersionTest, ShowsKoansVersionThenChromiums) {
  const std::string display = DisplayVersion();
  EXPECT_FALSE(Full().empty());
  EXPECT_TRUE(base::StartsWith(Full(), display.substr(0, display.find(' '))))
      << display;
  EXPECT_TRUE(base::EndsWith(display,
                             std::string(" Chromium ") +
                                 std::string(version_info::GetVersionNumber())))
      << display;
}

}  // namespace
}  // namespace koan::version
