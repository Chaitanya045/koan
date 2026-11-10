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

TEST(WindowSyncModelTest, RoundTripsThroughPrefs) {
  WindowSyncModel model;
  model.SetSpaces(
      {{.id = "a", .name = u"Work", .icon = u"💼", .theme = "{}"}, Space("b")});
  ItemRecord pinned = Pinned("p", "b");
  pinned.custom_title = u"Mail";
  pinned.custom_icon = u"✉️";
  model.SetItems({Essential("e"), pinned});
  model.SetLive("e", 3);

  WindowSyncModel restored;
  restored.FromValue(model.ToValue());
  EXPECT_TRUE(restored.seeded());
  EXPECT_EQ(model.spaces(), restored.spaces());
  EXPECT_EQ(model.items(), restored.items());
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
