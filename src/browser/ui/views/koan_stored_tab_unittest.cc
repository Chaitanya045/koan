// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_stored_tab.h"

#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

TEST(KoanStoredTabTest, RoundTrips) {
  const StoredTab tab(GURL("https://example.com/a"), u"Example",
                      {{"koan.pinned_tab", R"({"pinned_in_space":true})"}});
  const std::optional<StoredTab> back =
      StoredTab::FromValue(base::Value(tab.ToValue()));
  ASSERT_TRUE(back);
  EXPECT_EQ(tab, *back);
}

TEST(KoanStoredTabTest, SkipsBrokenEntries) {
  // The list is read back from the profile's prefs, which an old or edited
  // profile may have in any shape.
  base::ListValue list;
  list.Append("not a dict");
  list.Append(base::DictValue().Set("title", "no url"));
  list.Append(base::DictValue().Set("url", "not a url"));
  list.Append(StoredTab(GURL("https://kept.example/"), u"", {}).ToValue());
  const std::vector<StoredTab> tabs = StoredTab::FromList(&list);
  ASSERT_EQ(1u, tabs.size());
  EXPECT_EQ(GURL("https://kept.example/"), tabs[0].url);
  EXPECT_TRUE(StoredTab::FromList(nullptr).empty());
}

TEST(KoanStoredTabTest, KeepsOnlyKoanSessionData) {
  // Restored as the tab's extra data, where only Koan's keys belong.
  base::DictValue entry =
      StoredTab(GURL("https://example.com/"), u"", {}).ToValue();
  entry.Set("data", base::DictValue()
                        .Set("koan.pinned_tab", "{}")
                        .Set("other", "x")
                        .Set("koan.number", 1));
  const std::optional<StoredTab> tab =
      StoredTab::FromValue(base::Value(std::move(entry)));
  ASSERT_TRUE(tab);
  EXPECT_EQ((std::map<std::string, std::string>{{"koan.pinned_tab", "{}"}}),
            tab->data);
}

}  // namespace koan
