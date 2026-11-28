// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_settings_rows.h"

#include <string_view>

#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::settings {
namespace {

// Words that only make sense on a Mac: its name, its keys and its apps.
constexpr std::string_view kMacWords[] = {
    "Mac", "macOS", "⌘", "⌥", "Command", "Option", "Finder", "Dock",
};

void ExpectNoMacWords(const char* text, std::string_view where) {
  if (!text) {
    return;
  }
  const std::string_view view(text);
  for (std::string_view word : kMacWords) {
    EXPECT_EQ(view.find(word), std::string_view::npos)
        << where << " says \"" << word << "\": " << view;
  }
}

// Off macOS (#504) no pane, group, row, option or note names the Mac, its
// keys or its settings, whether or not the row is shown on this machine.
TEST(KoanSettingsRowsTest, NoMacWordsOffMac) {
#if BUILDFLAG(IS_MAC)
  GTEST_SKIP() << "The Mac's text may name the Mac";
#else
  for (const Pane& pane : Panes()) {
    ExpectNoMacWords(pane.title, pane.id);
  }
  for (const Group& group : Groups()) {
    ExpectNoMacWords(group.section, group.id);
    ExpectNoMacWords(group.header, group.id);
    ExpectNoMacWords(group.description, group.id);
  }
  for (const Row& row : Rows()) {
    ExpectNoMacWords(row.label, row.pref);
    ExpectNoMacWords(row.note, row.pref);
    for (const Option& option : row.options) {
      ExpectNoMacWords(option.label, row.pref);
    }
  }
#endif
}

}  // namespace
}  // namespace koan::settings
