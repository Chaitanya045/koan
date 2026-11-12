// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/importer/koan_imported_spaces.h"

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace koan::importer {

namespace {

// StorableSidebar.json's shape (lists of key, value pairs; the "global"
// container is the window's sidebar), with Arc's Space icons and colours.
constexpr char kArcSidebar[] = R"({"sidebar": {"containers": [
  {"littleBrowser": {}}, {"spaces": ["x", {"id": "x", "title": "Little"}]},
  {"global": {}}, {
    "topAppsContainerIDs": [{"default": true}, "top",
                            {"custom": {"_0": {"directoryBasename":
                                               "Profile 1"}}}, "top1"],
    "spaces": [
      "s1", {"id": "s1", "title": " Work ", "profile": {"default": true},
             "containerIDs": ["unpinned", "u1", "pinned", "p1"],
             "customInfo": {"iconType": {"emoji_v2": "💼"},
               "windowTheme": {"background": {"single": {"_0": {"style": {
                 "color": {"_0": {"blendedGradient": {"_0": {
                   "baseColors": [{"red": 1, "green": 0, "blue": 0,
                                   "alpha": 1},
                                  {"red": 1, "green": 0, "blue": 0}],
                   "overlayColors": [{"red": 0, "green": 0.5, "blue": 1},
                                     {"red": 0, "green": 1, "blue": 0,
                                      "alpha": 0}],
                   "modifiers": {"intensityFactor": 0.9,
                                 "noiseFactor": 0.5}}}}}}}}}}}},
      "s2", {"id": "thebrowser.company.defaultPersonalSpaceID",
             "containerIDs": ["pinned", "p2"],
             "customInfo": {"iconType": {"emoji": 127968},
               "windowTheme": {"background": {"single": {"_0": {"style": {
                 "color": {"_0": {"blendedSingleColor": {"_0": {
                   "color": {"red": 0, "green": 0, "blue": 0,
                             "alpha": 0}}}}}}}}}}}},
      "s3", {"id": "s3", "title": "Other",
             "profile": {"custom": {"_0": {"directoryBasename":
                                           "Profile 1"}}},
             "containerIDs": ["pinned", "p3"],
             "customInfo": {"iconType": {"emoji_v2": "W"}}},
      "s4", {"id": "s4", "containerIDs": ["unpinned", "u4"]}
    ],
    "items": [
      "top", {"id": "top", "childrenIds": ["mail", "tools"]},
      "mail", {"id": "mail", "title": "Mail", "data": {"tab": {
               "savedURL": "https://mail.example/", "savedTitle": ""}}},
      "tools", {"id": "tools", "title": "Tools", "data": {"list": {}},
                "childrenIds": ["cal"]},
      "cal", {"id": "cal", "data": {"tab": {
              "savedURL": "https://cal.example/", "savedTitle": "Calendar"}}},
      "top1", {"id": "top1", "childrenIds": ["work"]},
      "p1", {"id": "p1",
             "childrenIds": ["docs", "project", "empty", "split", "bad"]},
      "docs", {"id": "docs", "data": {"tab": {
               "savedURL": "https://docs.example/", "savedTitle": "Docs"}}},
      "project", {"id": "project", "title": "Project", "data": {"list": {}},
                  "childrenIds": ["inner"]},
      "inner", {"id": "inner", "data": {"list": {}}, "childrenIds": ["spec"]},
      "spec", {"id": "spec", "data": {"tab": {
               "savedURL": "https://spec.example/"}}},
      "empty", {"id": "empty", "title": "Empty", "data": {"list": {}}},
      "split", {"id": "split", "data": {"splitView": {}}},
      "left", {"id": "left", "parentID": "split", "title": "Left",
               "data": {"tab": {"savedURL": "https://left.example/"}}},
      "right", {"id": "right", "parentID": "split",
                "data": {"tab": {"savedURL": "arc://settings"}}},
      "bad", {"id": "bad", "data": {"tab": {"savedURL": "javascript:1"}}},
      "u1", {"id": "u1", "childrenIds": ["temp"]},
      "temp", {"id": "temp", "data": {"tab": {
               "savedURL": "https://temporary.example/"}}},
      "p2", {"id": "p2"},
      "p3", {"id": "p3", "childrenIds": ["work"]},
      "work", {"id": "work", "data": {"tab": {
               "savedURL": "https://work.example/"}}}
    ]
  }
]}})";

// A theme's colours ("c" of each gradientColors entry) and its opacity and
// texture, from its JSON.
struct Theme {
  std::vector<std::vector<int>> colors;
  std::vector<bool> primary;
  std::vector<int> lightness;
  double opacity = 0;
  double texture = 0;
};
std::optional<Theme> ParseTheme(const std::string& json) {
  const std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const base::ListValue* list =
      dict ? dict->FindList("gradientColors") : nullptr;
  if (!list) {
    return std::nullopt;
  }
  Theme theme;
  for (const base::Value& entry : *list) {
    std::vector<int> c;
    for (const base::Value& channel : *entry.GetDict().FindList("c")) {
      c.push_back(channel.GetInt());
    }
    theme.colors.push_back(std::move(c));
    theme.primary.push_back(*entry.GetDict().FindBool("isPrimary"));
    theme.lightness.push_back(*entry.GetDict().FindInt("lightness"));
  }
  theme.opacity = *dict->FindDouble("opacity");
  theme.texture = *dict->FindDouble("texture");
  return theme;
}

// Zen 1.24's zen-sessions.jsonlz4, decompressed (ZenSessionManager): the
// Spaces, the tabs (pinned ones with their Space and folder) and the folders
// (tab groups, nested by parentId).
constexpr char kZenSessions[] = R"({"lastCollected": 1792584089850,
  "spaces": [
    {"uuid": "{w1}", "name": "Work", "icon": "💼",
     "containerTabId": 0, "hasCollapsedPinnedTabs": false,
     "theme": {"type": "gradient", "gradientColors": [
       {"c": [10, 20, 30], "isCustom": false, "algorithm": "complementary",
        "isPrimary": true, "lightness": 40, "type": "explicit-lightness"}],
       "opacity": 0.6, "texture": 0.25}},
    {"uuid": "{w2}", "name": "  ",
     "icon": "chrome://browser/skin/zen-icons/selectable/airplane.svg",
     "theme": {"type": "gradient", "gradientColors": [], "opacity": 0.5,
               "texture": 0}},
    {"uuid": "{w1}", "name": "Duplicate"},
    {"name": "No uuid"}
  ],
  "folders": [
    {"pinned": true, "splitViewGroup": false, "id": "f1", "name": "Research",
     "collapsed": true, "parentId": null, "workspaceId": "{w1}",
     "prevSiblingInfo": {"type": "start", "id": null}},
    {"pinned": true, "splitViewGroup": false, "id": "f2", "name": "Papers",
     "collapsed": false, "parentId": "f1", "workspaceId": "{w1}"},
    {"pinned": true, "splitViewGroup": true, "id": "sv", "parentId": "f1",
     "workspaceId": "{w1}"},
    {"pinned": true, "id": "later", "name": "Later", "parentId": null,
     "workspaceId": "{w2}"},
    {"pinned": true, "id": "live", "name": "Feed", "isLiveFolder": true,
     "workspaceId": "{w1}"}
  ],
  "groups": [],
  "tabs": [
    {"pinned": true, "zenEssential": true, "zenWorkspace": "{w1}",
     "entries": [{"url": "https://mail.example/", "title": "Mail"}],
     "index": 1},
    {"pinned": true, "zenWorkspace": "{w1}", "zenIsEmpty": true,
     "groupId": "f1", "entries": [{"url": "about:blank"}], "index": 1},
    {"pinned": true, "zenWorkspace": "{w1}", "zenStaticLabel": "My docs",
     "_zenPinnedInitialState": {"entry": {"url": "https://docs.example/",
                                          "title": "Docs"}, "image": null},
     "entries": [{"url": "https://docs.example/page2", "title": "Page 2"}],
     "index": 1},
    {"pinned": true, "zenWorkspace": "{w1}", "groupId": "f2",
     "entries": [{"url": "https://a.example/", "title": "A"},
                 {"url": "https://paper.example/", "title": "Paper"}],
     "index": 2},
    {"pinned": true, "zenWorkspace": "{w1}", "groupId": "sv",
     "entries": [{"url": "https://left.example/", "title": "Left"}],
     "index": 1},
    {"pinned": true, "zenWorkspace": "{w1}", "groupId": "f1",
     "entries": [{"url": "https://notes.example/", "title": "Notes"}],
     "index": 1},
    {"pinned": true, "zenWorkspace": "{w1}", "groupId": "live",
     "zenLiveFolderItemId": "item1",
     "entries": [{"url": "https://feed.example/"}], "index": 1},
    {"pinned": true, "zenWorkspace": "{w2}", "zenIsGlance": true,
     "entries": [{"url": "https://glance.example/"}], "index": 1},
    {"pinned": true, "zenWorkspace": "{w2}",
     "entries": [{"url": "https://w2.example/", "title": "W2"}], "index": 9},
    {"pinned": true, "zenWorkspace": "{gone}",
     "entries": [{"url": "https://gone.example/"}], "index": 1},
    {"pinned": true, "zenWorkspace": "{w2}",
     "entries": [{"url": "about:preferences"}], "index": 1},
    {"pinned": false, "zenWorkspace": "{w1}",
     "entries": [{"url": "https://unpinned.example/"}], "index": 1}
  ]})";

}  // namespace

TEST(KoanImportedSpacesTest, ArcSpaces) {
  const ImportedSpaces plan = ParseArcSpaces(kArcSidebar, "Default");
  // The Favorites, with Arc's folders (the Essentials take their tabs).
  ASSERT_EQ(2u, plan.essentials.size());
  EXPECT_EQ(GURL("https://mail.example/"), plan.essentials[0].url);
  EXPECT_EQ(u"Mail", plan.essentials[0].title);
  EXPECT_TRUE(plan.essentials[1].is_folder);
  ASSERT_EQ(1u, plan.essentials[1].children.size());
  EXPECT_EQ(u"Calendar", plan.essentials[1].children[0].title);

  // The main profile's Spaces, in order; the Little Arc container's never.
  ASSERT_EQ(3u, plan.spaces.size());
  const ImportedSpace& work = plan.spaces[0];
  EXPECT_EQ(u"Work", work.name);
  EXPECT_EQ(u"\U0001F4BC", work.icon);
  // Its window colours as Zen's import makes them: distinct, opaque ones,
  // at the primary's lightness; Arc's intensity and noise as Zen's opacity
  // and grain.
  const std::optional<Theme> theme = ParseTheme(work.theme);
  ASSERT_TRUE(theme) << work.theme;
  EXPECT_EQ((std::vector<std::vector<int>>{{255, 0, 0}, {0, 128, 255}}),
            theme->colors);
  EXPECT_EQ((std::vector<bool>{true, false}), theme->primary);
  EXPECT_EQ((std::vector<int>{50, 50}), theme->lightness);
  EXPECT_DOUBLE_EQ(0.8, theme->opacity);
  EXPECT_DOUBLE_EQ(0.5, theme->texture);
  // Its pinned tabs and folders; never its unpinned tabs, an empty folder or
  // a page Koan can't open. A split view's tabs where it was.
  ASSERT_EQ(3u, work.items.size());
  EXPECT_EQ(GURL("https://docs.example/"), work.items[0].url);
  EXPECT_EQ(u"Docs", work.items[0].title);
  const ImportedItem& project = work.items[1];
  EXPECT_TRUE(project.is_folder);
  EXPECT_EQ(u"Project", project.title);
  // Zen's import shows Arc's folders collapsed.
  EXPECT_TRUE(project.collapsed);
  ASSERT_EQ(1u, project.children.size());
  EXPECT_TRUE(project.children[0].is_folder);
  EXPECT_TRUE(project.children[0].title.empty());
  ASSERT_EQ(1u, project.children[0].children.size());
  EXPECT_EQ(GURL("https://spec.example/"), project.children[0].children[0].url);
  EXPECT_EQ(GURL("https://left.example/"), work.items[2].url);
  EXPECT_EQ(u"Left", work.items[2].title);

  // Arc's default Space without a title is "Personal", its emoji a code
  // point; a fully transparent colour is no theme.
  EXPECT_EQ(u"Personal", plan.spaces[1].name);
  EXPECT_EQ(u"\U0001F3E0", plan.spaces[1].icon);
  EXPECT_TRUE(plan.spaces[1].theme.empty());
  EXPECT_TRUE(plan.spaces[1].items.empty());
  // Without a title, numbered among all the Spaces; without a pinned
  // container, no pinned tabs (but the Space).
  EXPECT_EQ(u"Space 4", plan.spaces[2].name);
  EXPECT_TRUE(plan.spaces[2].items.empty());

  // Another profile: its own Favorites and Spaces only. A letter isn't an
  // emoji icon.
  const ImportedSpaces other = ParseArcSpaces(kArcSidebar, "Profile 1");
  ASSERT_EQ(1u, other.essentials.size());
  EXPECT_EQ(GURL("https://work.example/"), other.essentials[0].url);
  ASSERT_EQ(1u, other.spaces.size());
  EXPECT_EQ(u"Other", other.spaces[0].name);
  EXPECT_TRUE(other.spaces[0].icon.empty());
  ASSERT_EQ(1u, other.spaces[0].items.size());

  EXPECT_TRUE(ParseArcSpaces("{}", "Default").empty());
  EXPECT_TRUE(ParseArcSpaces("not json", "Default").empty());
  EXPECT_TRUE(
      ParseArcSpaces(R"({"sidebar": {"containers": [1, 2, 3]}})", "Default")
          .empty());
}

TEST(KoanImportedSpacesTest, ZenSessions) {
  const ImportedSpaces plan = ParseZenSessions(kZenSessions);
  ASSERT_EQ(1u, plan.essentials.size());
  EXPECT_EQ(GURL("https://mail.example/"), plan.essentials[0].url);
  EXPECT_EQ(u"Mail", plan.essentials[0].title);

  // Every Space once, in order.
  ASSERT_EQ(2u, plan.spaces.size());
  const ImportedSpace& work = plan.spaces[0];
  EXPECT_EQ(u"Work", work.name);
  EXPECT_EQ(u"\U0001F4BC", work.icon);
  // Zen's theme object as it is.
  const std::optional<Theme> theme = ParseTheme(work.theme);
  ASSERT_TRUE(theme) << work.theme;
  EXPECT_EQ((std::vector<std::vector<int>>{{10, 20, 30}}), theme->colors);
  EXPECT_DOUBLE_EQ(0.6, theme->opacity);
  EXPECT_DOUBLE_EQ(0.25, theme->texture);

  // The pinned tabs at their pinned URL, with their label; the folders where
  // their first tab was, nested; a split view's tab in the folder holding
  // it. No empty, Glance or live-folder tabs, nor unpinned ones.
  ASSERT_EQ(2u, work.items.size());
  EXPECT_FALSE(work.items[0].is_folder);
  EXPECT_EQ(GURL("https://docs.example/"), work.items[0].url);
  EXPECT_EQ(u"Docs", work.items[0].title);
  EXPECT_EQ(u"My docs", work.items[0].label);
  const ImportedItem& research = work.items[1];
  EXPECT_TRUE(research.is_folder);
  EXPECT_EQ(u"Research", research.title);
  EXPECT_TRUE(research.collapsed);
  ASSERT_EQ(3u, research.children.size());
  const ImportedItem& papers = research.children[0];
  EXPECT_TRUE(papers.is_folder);
  EXPECT_EQ(u"Papers", papers.title);
  EXPECT_FALSE(papers.collapsed);
  ASSERT_EQ(1u, papers.children.size());
  // A tab without a pinned URL: the page it shows (Firefox's 1-based index).
  EXPECT_EQ(GURL("https://paper.example/"), papers.children[0].url);
  EXPECT_EQ(u"Paper", papers.children[0].title);
  EXPECT_EQ(GURL("https://left.example/"), research.children[1].url);
  EXPECT_EQ(GURL("https://notes.example/"), research.children[2].url);

  // An unnamed Space is Zen's "Space"; Zen's SVG icons and a theme without
  // colours aren't kept. An empty folder comes along.
  const ImportedSpace& second = plan.spaces[1];
  EXPECT_EQ(u"Space", second.name);
  EXPECT_TRUE(second.icon.empty());
  EXPECT_TRUE(second.theme.empty());
  ASSERT_EQ(2u, second.items.size());
  // An index past the entries is the last one.
  EXPECT_EQ(GURL("https://w2.example/"), second.items[0].url);
  EXPECT_TRUE(second.items[1].is_folder);
  EXPECT_EQ(u"Later", second.items[1].title);
  EXPECT_TRUE(second.items[1].children.empty());

  EXPECT_TRUE(ParseZenSessions("").empty());
  EXPECT_TRUE(ParseZenSessions(R"({"spaces": 3, "tabs": {}})").empty());
}

// A folder that names itself or one of its subfolders as its parent (a
// broken file) never loops: it goes at the top of its Space.
TEST(KoanImportedSpacesTest, ZenFolderCycle) {
  const ImportedSpaces plan = ParseZenSessions(R"({
    "spaces": [{"uuid": "w", "name": "W"}],
    "folders": [{"id": "a", "name": "A", "parentId": "b", "workspaceId": "w"},
                {"id": "b", "name": "B", "parentId": "a", "workspaceId": "w"},
                {"id": "c", "name": "C", "parentId": "c", "workspaceId": "w"}],
    "tabs": [{"pinned": true, "zenWorkspace": "w", "groupId": "a",
              "entries": [{"url": "https://a.example/"}]},
             {"pinned": true, "zenWorkspace": "w", "groupId": "c",
              "entries": [{"url": "https://c.example/"}]}]})");
  ASSERT_EQ(1u, plan.spaces.size());
  const std::vector<ImportedItem>& items = plan.spaces[0].items;
  ASSERT_EQ(3u, items.size());
  EXPECT_EQ(u"A", items[0].title);
  ASSERT_EQ(1u, items[0].children.size());
  EXPECT_EQ(GURL("https://a.example/"), items[0].children[0].url);
  EXPECT_EQ(u"C", items[1].title);
  ASSERT_EQ(1u, items[1].children.size());
  EXPECT_EQ(GURL("https://c.example/"), items[1].children[0].url);
  EXPECT_EQ(u"B", items[2].title);
  EXPECT_TRUE(items[2].children.empty());
}

// Zen's profile folder: zen-sessions.jsonlz4, mozLz4 (a block of literals
// here: any LZ4 decoder reads it).
TEST(KoanImportedSpacesTest, ReadZenSpaces) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  EXPECT_TRUE(ReadZenSpaces(dir.GetPath()).empty());

  const std::string json = R"({"spaces": [{"uuid": "w", "name": "Home"}]})";
  std::string file("mozLz40\0", 8);
  const uint32_t size = static_cast<uint32_t>(json.size());
  for (int shift = 0; shift < 32; shift += 8) {
    file.push_back(static_cast<char>((size >> shift) & 0xff));
  }
  // The token: 15 literals and more (in the next byte), no match.
  file.push_back(static_cast<char>(0xf0));
  file.push_back(static_cast<char>(json.size() - 15));
  file += json;
  ASSERT_TRUE(
      base::WriteFile(dir.GetPath().AppendASCII(kZenSessionsFile), file));
  const ImportedSpaces plan = ReadZenSpaces(dir.GetPath());
  ASSERT_EQ(1u, plan.spaces.size());
  EXPECT_EQ(u"Home", plan.spaces[0].name);

  // Not mozLz4: nothing.
  ASSERT_TRUE(
      base::WriteFile(dir.GetPath().AppendASCII(kZenSessionsFile), json));
  EXPECT_TRUE(ReadZenSpaces(dir.GetPath()).empty());
}

}  // namespace koan::importer
