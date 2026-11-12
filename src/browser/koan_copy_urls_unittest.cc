// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_copy_urls.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {

TEST(KoanCopyUrlsTest, OneUrlPerLineInOrder) {
  size_t count = 0;
  EXPECT_EQ(FormatCopiedUrls({{u"B", u"https://b.test/"},
                              {u"A", u"https://a.test/"},
                              {u"C", u"https://c.test/"}},
                             /*markdown=*/false, &count),
            u"https://b.test/\nhttps://a.test/\nhttps://c.test/");
  EXPECT_EQ(count, 3u);
}

TEST(KoanCopyUrlsTest, SkipsTabsWithoutUrl) {
  size_t count = 0;
  EXPECT_EQ(FormatCopiedUrls({{u"Empty", u""},
                              {u"A", u"https://a.test/"},
                              {u"Empty", u""},
                              {u"B", u"https://b.test/"}},
                             /*markdown=*/false, &count),
            u"https://a.test/\nhttps://b.test/");
  EXPECT_EQ(count, 2u);
  EXPECT_EQ(FormatCopiedUrls({{u"Empty", u""}}, /*markdown=*/true, &count),
            u"");
  EXPECT_EQ(count, 0u);
}

TEST(KoanCopyUrlsTest, MarkdownOneLinkPerLine) {
  EXPECT_EQ(FormatCopiedUrls(
                {{u"Alpha", u"https://a.test/"}, {u"", u"https://b.test/"}},
                /*markdown=*/true),
            u"[Alpha](https://a.test/)\n[https://b.test/](https://b.test/)");
}

TEST(KoanCopyUrlsTest, MarkdownEscapesTitleAndUrl) {
  EXPECT_EQ(EscapeMarkdownLinkText(u"a [b] (c) \\d"),
            u"a \\[b\\] \\(c\\) \\\\d");
  EXPECT_EQ(EscapeMarkdownLinkText(u"two\nlines\r"), u"two lines ");
  EXPECT_EQ(FormatCopiedUrls({{u"Foo](x) [y", u"https://w.test/A_(b)"}},
                             /*markdown=*/true),
            u"[Foo\\](x\\) \\[y](https://w.test/A_%28b%29)");
}

}  // namespace koan
