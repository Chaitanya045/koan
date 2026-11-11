// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/window_sync/koan_window_sync_model.h"

#include <string>
#include <vector>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::window_sync {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using Kind = ItemRecord::Kind;

SpaceRecord Space(std::string id, std::u16string name = u"Space") {
  return {.id = std::move(id), .name = std::move(name)};
}

ItemRecord Essential(std::string id) {
  return {.id = std::move(id), .url = "https://example.com/"};
}

ItemRecord Pinned(std::string id, std::string space) {
  return {.id = std::move(id),
          .kind = Kind::kPinned,
          .space_id = std::move(space),
          .url = "https://example.org/"};
}

FolderRecord Folder(std::string id,
                    std::string space,
                    std::string parent = std::string()) {
  return {.id = std::move(id),
          .name = u"Folder",
          .space_id = std::move(space),
          .parent_id = std::move(parent)};
}

std::vector<std::string> FolderIds(const WindowSyncModel& model) {
  std::vector<std::string> ids;
  for (const FolderRecord& folder : model.folders()) {
    ids.push_back(folder.id);
  }
  return ids;
}

std::vector<std::string> AddedIds(const Plan<SpaceRecord>& plan) {
  std::vector<std::string> ids;
  for (const auto& [record, index] : plan.add) {
    ids.push_back(record.id + "@" + std::to_string(index));
  }
  return ids;
}

TEST(WindowSyncPlanTest, SameStateNeedsNothing) {
  const std::vector<SpaceRecord> spaces = {Space("a"), Space("b")};
  EXPECT_TRUE(PlanSpaces(spaces, spaces).empty());
}

// A new window (one Space of its own) takes every Space of the others.
TEST(WindowSyncPlanTest, NewWindowTakesTheSpaces) {
  const Plan<SpaceRecord> plan =
      PlanSpaces({Space("own")}, {Space("a"), Space("b"), Space("c")});
  EXPECT_THAT(plan.remove, ElementsAre("own"));
  EXPECT_THAT(AddedIds(plan), ElementsAre("a@0", "b@1", "c@2"));
  EXPECT_THAT(plan.update, IsEmpty());
  EXPECT_FALSE(plan.order);
}

TEST(WindowSyncPlanTest, RenameIsAnUpdate) {
  const Plan<SpaceRecord> plan = PlanSpaces({Space("a", u"Work"), Space("b")},
                                            {Space("a", u"Play"), Space("b")});
  ASSERT_EQ(1u, plan.update.size());
  EXPECT_EQ(u"Play", plan.update[0].name);
  EXPECT_THAT(plan.remove, IsEmpty());
  EXPECT_FALSE(plan.order);
}

// Reordering in one window moves the Spaces of the others; an addition at its
// index alone isn't a reorder.
TEST(WindowSyncPlanTest, OrderOnlyWhenKeptSpacesMove) {
  const Plan<SpaceRecord> moved =
      PlanSpaces({Space("a"), Space("b"), Space("c")},
                 {Space("c"), Space("a"), Space("b")});
  ASSERT_TRUE(moved.order);
  EXPECT_THAT(*moved.order, ElementsAre("c", "a", "b"));

  const Plan<SpaceRecord> added = PlanSpaces(
      {Space("a"), Space("c")}, {Space("a"), Space("b"), Space("c")});
  EXPECT_THAT(AddedIds(added), ElementsAre("b@1"));
  EXPECT_FALSE(added.order);
}

TEST(WindowSyncPlanTest, DuplicateLocalIdIsRemoved) {
  const Plan<SpaceRecord> plan =
      PlanSpaces({Space("a"), Space("a")}, {Space("a")});
  EXPECT_THAT(plan.remove, ElementsAre("a"));
  EXPECT_THAT(plan.add, IsEmpty());
}

TEST(WindowSyncPlanTest, ItemMovingToAnotherSpaceIsAnUpdate) {
  const Plan<ItemRecord> plan =
      PlanItems({Pinned("x", "a")}, {Pinned("x", "b")});
  ASSERT_EQ(1u, plan.update.size());
  EXPECT_EQ("b", plan.update[0].space_id);
}

// A new window's own Space (with its startup tabs) becomes one of the synced
// Spaces rather than being deleted: by name first, then in order.
TEST(WindowSyncPlanTest, JoiningWindowAdoptsItsSpaces) {
  const std::vector<SpaceRecord> target = {
      Space("a", u"Work"), Space("b", u"Home"), Space("c", u"Play")};
  EXPECT_THAT(
      AdoptSpaces({Space("", u"Home"), Space("", u"Other")}, target, {}),
      ElementsAre("b", "a"));
  // Ids the window has already are not adopted twice.
  EXPECT_THAT(AdoptSpaces({Space("", u"Work")}, target, {"a"}),
              ElementsAre("b"));
  // More local Spaces than free targets: the rest adopt nothing.
  EXPECT_THAT(AdoptSpaces({Space(""), Space(""), Space("")}, {Space("a")}, {}),
              ElementsAre("a", "", ""));
}

// The first window of a start (its Spaces restored with new tab group ids)
// takes back only the saved ids of the Spaces it has: no saved Space is
// renamed into another one, and a saved Space it lacks stays unmatched (it
// is not added to the window).
TEST(WindowSyncPlanTest, FirstWindowTakesBackOnlyMatchingSavedIds) {
  const std::vector<SpaceRecord> saved = {
      Space("a", u"Space"), Space("b", u"Work"), Space("c", u"Gone")};
  EXPECT_THAT(AdoptSavedSpaces(
                  {Space("", u"Work"), Space("", u"Space"), Space("", u"New")},
                  saved, {}),
              ElementsAre("b", "a", ""));
  // Two Spaces with one name take the saved ones in order, each once.
  EXPECT_THAT(AdoptSavedSpaces({Space(""), Space(""), Space("")},
                               {Space("a"), Space("b")}, {}),
              ElementsAre("a", "b", ""));
  // A different icon is a different Space; ids the window has aren't taken.
  SpaceRecord rocket = Space("", u"Work");
  rocket.icon = u"\U0001F680";
  EXPECT_THAT(AdoptSavedSpaces({rocket, Space("", u"Space")}, saved, {"a"}),
              ElementsAre("", ""));
}

TEST(WindowSyncModelTest, FirstWindowSeeds) {
  WindowSyncModel model;
  EXPECT_FALSE(model.seeded());
  // A window with no Spaces yet doesn't seed (or wipe) anything.
  EXPECT_FALSE(model.SetSpaces({}));
  EXPECT_FALSE(model.seeded());
  EXPECT_TRUE(model.SetSpaces({Space("a")}));
  EXPECT_TRUE(model.seeded());
  EXPECT_FALSE(model.SetSpaces({Space("a")}));
}

TEST(WindowSyncModelTest, DeletingASpaceDropsItsPinnedTabsAndPages) {
  WindowSyncModel model;
  model.SetSpaces({Space("a"), Space("b")});
  model.SetItems({Essential("e"), Pinned("p", "b")});
  model.SetLive("p", 7);
  model.SetSpaces({Space("a")});
  ASSERT_EQ(1u, model.items().size());
  EXPECT_EQ("e", model.items()[0].id);
  EXPECT_EQ(kNoWindow, model.LiveWindow("p"));
}

TEST(WindowSyncModelTest, ItemsInUnknownSpacesAreDropped) {
  WindowSyncModel model;
  model.SetSpaces({Space("a")});
  ItemRecord essential_with_space = Essential("bad");
  essential_with_space.space_id = "a";
  model.SetItems({Pinned("p", "gone"), essential_with_space, Essential("e"),
                  Essential("e")});
  ASSERT_EQ(1u, model.items().size());
  EXPECT_EQ("e", model.items()[0].id);
}

// Zen keeps one page per synced tab: shown in another window, it moves there.
TEST(WindowSyncModelTest, ShowMovesThePage) {
  WindowSyncModel model;
  model.SetSpaces({Space("a")});
  model.SetItems({Essential("e")});
  EXPECT_EQ(ShowDecision{.kind = ShowDecision::Kind::kLoad},
            model.RequestShow("e", 1));
  model.SetLive("e", 1);
  EXPECT_EQ(ShowDecision{}, model.RequestShow("e", 1));
  EXPECT_EQ((ShowDecision{.kind = ShowDecision::Kind::kMove, .from = 1}),
            model.RequestShow("e", 2));
  // Unknown items and windows ask for nothing.
  EXPECT_EQ(ShowDecision{}, model.RequestShow("nope", 2));
  EXPECT_EQ(ShowDecision{}, model.RequestShow("e", kNoWindow));
  // A page can't be live for an item the model doesn't have.
  model.SetLive("nope", 2);
  EXPECT_EQ(kNoWindow, model.LiveWindow("nope"));
}

TEST(WindowSyncModelTest, ClosingWindowHandsItsPagesOver) {
  WindowSyncModel model;
  model.SetSpaces({Space("a")});
  model.SetItems({Essential("e"), Essential("f"), Pinned("p", "a")});
  model.SetLive("e", 1);
  model.SetLive("f", 2);
  model.SetLive("p", 1);
  EXPECT_THAT(model.OnWindowClosing(1, 2), ElementsAre("e", "p"));
  // The last window, or quitting: nothing to hand over to.
  EXPECT_THAT(model.OnWindowClosing(1, kNoWindow), IsEmpty());
  EXPECT_THAT(model.OnWindowClosing(1, 1), IsEmpty());
  model.OnWindowClosed(1);
  EXPECT_EQ(kNoWindow, model.LiveWindow("e"));
  EXPECT_EQ(2, model.LiveWindow("f"));
}

// A folder lives in a Space's pinned section, inside its parent.
TEST(WindowSyncModelTest, FoldersNeedTheirSpaceAndParent) {
  WindowSyncModel model;
  model.SetSpaces({Space("a"), Space("b")});
  EXPECT_TRUE(model.SetFolders(
      {Folder("f", "a"), Folder("sub", "a", "f"),
       Folder("other-space", "b", "f"), Folder("lost", "a", "nope"),
       Folder("gone", "c"), Folder("child-first", "a", "late"),
       Folder("late", "a")}));
  EXPECT_THAT(FolderIds(model), ElementsAre("f", "sub", "late"));
  EXPECT_FALSE(model.SetFolders(
      {Folder("f", "a"), Folder("sub", "a", "f"), Folder("late", "a")}));
  // Deleting the Space deletes its folders.
  model.SetSpaces({Space("b")});
  EXPECT_THAT(model.folders(), IsEmpty());
}

TEST(WindowSyncModelTest, TabsLeaveFoldersThatAreGone) {
  WindowSyncModel model;
  model.SetSpaces({Space("a"), Space("b")});
  model.SetFolders({Folder("f", "a")});
  ItemRecord in_folder = Pinned("p", "a");
  in_folder.folder_id = "f";
  ItemRecord wrong_space = Pinned("q", "b");
  wrong_space.folder_id = "f";
  ItemRecord essential = Essential("e");
  essential.folder_id = "f";
  model.SetItems({essential, in_folder, wrong_space});
  ASSERT_EQ(3u, model.items().size());
  EXPECT_EQ("", model.items()[0].folder_id);
  EXPECT_EQ("f", model.items()[1].folder_id);
  EXPECT_EQ("", model.items()[2].folder_id);
  // The folder went in another window: its tabs stay, out of it.
  EXPECT_TRUE(model.SetFolders({}));
  EXPECT_EQ("", model.items()[1].folder_id);
}

TEST(WindowSyncPlanTest, RenamingAFolderIsAnUpdate) {
  FolderRecord renamed = Folder("f", "a");
  renamed.name = u"Renamed";
  const Plan<FolderRecord> plan = PlanFolders(
      {Folder("f", "a"), Folder("g", "a")}, {Folder("g", "a"), renamed});
  EXPECT_THAT(plan.remove, IsEmpty());
  EXPECT_THAT(plan.add, IsEmpty());
  ASSERT_EQ(1u, plan.update.size());
  EXPECT_EQ(u"Renamed", plan.update[0].name);
  EXPECT_TRUE(plan.order);
}

// Zen keeps a folder's collapsed state only in the session: a window keeps
// its own, and only a new folder (a new window, a restore) takes it.
TEST(WindowSyncPlanTest, CollapsingAFolderIsNotSyncedLive) {
  FolderRecord collapsed = Folder("f", "a");
  collapsed.collapsed = true;
  Plan<FolderRecord> plan = PlanFolders({Folder("f", "a")}, {collapsed});
  EXPECT_TRUE(plan.empty());
  // With another change it is an update; the client leaves the collapsed
  // state of a folder it has.
  collapsed.name = u"Renamed";
  plan = PlanFolders({Folder("f", "a")}, {collapsed});
  ASSERT_EQ(1u, plan.update.size());
  // A new window gets the folder collapsed.
  plan = PlanFolders({}, {collapsed});
  ASSERT_EQ(1u, plan.add.size());
  EXPECT_TRUE(plan.add[0].first.collapsed);
}

TEST(WindowSyncModelTest, CollapsingAFolderIsSavedButNotAChange) {
  WindowSyncModel model;
  model.SetSpaces({Space("a")});
  EXPECT_TRUE(model.SetFolders({Folder("f", "a")}));
  FolderRecord collapsed = Folder("f", "a");
  collapsed.collapsed = true;
  EXPECT_FALSE(model.SetFolders({collapsed}));
  ASSERT_EQ(1u, model.folders().size());
  EXPECT_TRUE(model.folders()[0].collapsed);
  // And it is restored with the rest.
  WindowSyncModel restored;
  restored.FromValue(model.ToValue());
  ASSERT_EQ(1u, restored.folders().size());
  EXPECT_TRUE(restored.folders()[0].collapsed);
}

// Zen's TabClose and TabUnpinned: a synced tab closed, unpinned or taken out
// of the Essentials in one window closes in every window. The window reports
// all of those in `closed`; an item that leaves without being named there
// stays where its page is.
TEST(WindowSyncModelTest, ClosedOrUnpinnedItemsCloseEverywhere) {
  WindowSyncModel model;
  model.SetSpaces({Space("a")});
  model.SetItems(
      {Essential("e"), Essential("f"), Pinned("p", "a"), Pinned("s", "a")});
  // "e" closed, "f" unpinned, "s" left unnamed (not in `closed`); "p" is
  // still synced, so naming it doesn't count.
  EXPECT_TRUE(model.SetItems({Pinned("p", "a")}, {"e", "f", "p"}));
  EXPECT_TRUE(model.WasClosed("e"));
  EXPECT_TRUE(model.WasClosed("f"));
  EXPECT_FALSE(model.WasClosed("p"));
  EXPECT_FALSE(model.WasClosed("s"));
  // A reopened (or re-pinned) tab keeps its id: it is an item again.
  EXPECT_TRUE(model.SetItems({Essential("e"), Pinned("p", "a")}));
  EXPECT_FALSE(model.WasClosed("e"));
}

TEST(WindowSyncModelTest, RoundTripsThroughPrefs) {
  WindowSyncModel model;
  model.SetSpaces(
      {{.id = "a", .name = u"Work", .icon = u"💼", .theme = "{}"}, Space("b")});
  ItemRecord pinned = Pinned("p", "b");
  pinned.custom_title = u"Mail";
  pinned.custom_icon = u"✉️";
  pinned.container = "user:s1";
  pinned.folder_id = "f";
  FolderRecord folder = Folder("f", "b");
  folder.name = u"Reading";
  folder.collapsed = true;
  model.SetFolders({folder, Folder("g", "b", "f")});
  model.SetItems({Essential("e"), pinned});
  model.SetLive("e", 3);

  WindowSyncModel restored;
  restored.FromValue(model.ToValue());
  EXPECT_TRUE(restored.seeded());
  EXPECT_EQ(model.spaces(), restored.spaces());
  EXPECT_EQ(model.folders(), restored.folders());
  EXPECT_EQ(model.items(), restored.items());
  EXPECT_EQ("f", restored.items()[1].folder_id);
  // Which window had a page isn't saved: the session decides.
  EXPECT_EQ(kNoWindow, restored.LiveWindow("e"));
}

TEST(WindowSyncModelTest, MalformedPrefsLeaveItUnseeded) {
  WindowSyncModel model;
  model.FromValue(base::DictValue());
  EXPECT_FALSE(model.seeded());
  model.FromValue(base::DictValue().Set("spaces", base::ListValue().Append(3)));
  EXPECT_FALSE(model.seeded());
  model.FromValue(
      base::DictValue()
          .Set("spaces",
               base::ListValue().Append(base::DictValue().Set("id", "a")))
          .Set("items", base::ListValue()
                            .Append(base::DictValue().Set("id", "x").Set(
                                "kind", "weird"))
                            .Append(5)));
  EXPECT_TRUE(model.seeded());
  EXPECT_THAT(model.items(), IsEmpty());
}

}  // namespace
}  // namespace koan::window_sync
