// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_user_containers.h"

#include <string>

#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::containers {
namespace {

using tab_groups::TabGroupColorId;

const ContainerId kWork{.kind = ContainerKind::kUser, .id = "swork"};
const ContainerId kHome{.kind = ContainerKind::kUser, .id = "shome"};

TEST(UserContainersTest, SpaceContainerIsASavedUserContainer) {
  EXPECT_EQ(kWork, SpaceContainerFromString("user:swork"));
  // None, an agent's, a throwaway one, garbage: no container.
  EXPECT_FALSE(SpaceContainerFromString(""));
  EXPECT_FALSE(SpaceContainerFromString("none"));
  EXPECT_FALSE(SpaceContainerFromString("agent:swork"));
  EXPECT_FALSE(SpaceContainerFromString("user:twork"));
  EXPECT_FALSE(SpaceContainerFromString("user:sWork"));
}

TEST(UserContainersTest, ParseDropsWhatIsNotAContainer) {
  base::ListValue list;
  list.Append(base::DictValue()
                  .Set("id", "swork")
                  .Set("name", "Work")
                  .Set("color", static_cast<int>(TabGroupColorId::kRed)));
  // Not a saved id, no name, not a dict, a repeated id.
  list.Append(base::DictValue().Set("id", "tthrow").Set("name", "Throwaway"));
  list.Append(base::DictValue().Set("id", "snoname"));
  list.Append("swork");
  list.Append(base::DictValue().Set("id", "swork").Set("name", "Again"));
  // An unknown colour is blue (grey is no container colour).
  list.Append(base::DictValue()
                  .Set("id", "shome")
                  .Set("name", "Home")
                  .Set("color", static_cast<int>(TabGroupColorId::kGrey)));
  const std::vector<UserContainer> parsed = ParseUserContainers(list);
  ASSERT_EQ(2u, parsed.size());
  EXPECT_EQ((UserContainer{
                .id = kWork, .name = "Work", .color = TabGroupColorId::kRed}),
            parsed[0]);
  EXPECT_EQ((UserContainer{
                .id = kHome, .name = "Home", .color = TabGroupColorId::kBlue}),
            parsed[1]);
}

TEST(UserContainersTest, AddTakesTheFirstFreeColourInFirefoxsOrder) {
  base::ListValue list;
  const std::optional<UserContainer> first =
      AddUserContainer(list, u"  Work  ");
  ASSERT_TRUE(first);
  EXPECT_EQ("Work", first->name);
  EXPECT_EQ(ContainerKind::kUser, first->id.kind);
  EXPECT_TRUE(first->id.persistent());
  EXPECT_EQ(TabGroupColorId::kBlue, first->color);
  const std::optional<UserContainer> second = AddUserContainer(list, u"Home");
  ASSERT_TRUE(second);
  EXPECT_EQ(TabGroupColorId::kCyan, second->color);
  EXPECT_NE(first->id, second->id);
  EXPECT_EQ((std::vector<UserContainer>{*first, *second}),
            ParseUserContainers(list));

  // Eight colours, then round again.
  for (int i = 0; i < 6; ++i) {
    ASSERT_TRUE(AddUserContainer(list, u"More"));
  }
  EXPECT_EQ(TabGroupColorId::kPurple, ParseUserContainers(list)[7].color);
  EXPECT_EQ(TabGroupColorId::kBlue, AddUserContainer(list, u"Ninth")->color);
}

TEST(UserContainersTest, AddRefusesBadNames) {
  base::ListValue list;
  EXPECT_FALSE(AddUserContainer(list, u""));
  EXPECT_FALSE(AddUserContainer(list, u"   "));
  EXPECT_FALSE(AddUserContainer(list, u"a\nb"));
  EXPECT_FALSE(AddUserContainer(list, std::u16string(65, u'x')));
  EXPECT_TRUE(list.empty());
  EXPECT_TRUE(AddUserContainer(list, std::u16string(64, u'x')));
}

TEST(UserContainersTest, ProxyIsReadValidated) {
  base::ListValue list;
  list.Append(base::DictValue()
                  .Set("id", "swork")
                  .Set("name", "Work")
                  .Set("proxy", base::DictValue().Set(
                                    "server", "socks5://127.0.0.1:1080")));
  // An invalid proxy reads as none; the container stays.
  list.Append(
      base::DictValue()
          .Set("id", "shome")
          .Set("name", "Home")
          .Set("proxy", base::DictValue().Set("server", "socks4://h:1")));
  const std::vector<UserContainer> parsed = ParseUserContainers(list);
  ASSERT_EQ(2u, parsed.size());
  EXPECT_EQ((ProxySetting{.server = "socks5://127.0.0.1:1080"}),
            parsed[0].proxy);
  EXPECT_FALSE(parsed[1].proxy);
}

TEST(UserContainersTest, SetProxySavesAndRemoves) {
  base::ListValue list;
  const std::optional<UserContainer> work = AddUserContainer(list, u"Work");
  const std::optional<UserContainer> home = AddUserContainer(list, u"Home");
  ASSERT_TRUE(work && home);
  const ProxySetting setting{.server = "socks5://127.0.0.1:1080"};

  EXPECT_TRUE(SetUserContainerProxy(list, work->id, setting));
  std::vector<UserContainer> parsed = ParseUserContainers(list);
  EXPECT_EQ(setting, parsed[0].proxy);
  EXPECT_FALSE(parsed[1].proxy);
  // The rest of the entry is kept.
  EXPECT_EQ("Work", parsed[0].name);
  EXPECT_EQ(work->color, parsed[0].color);

  EXPECT_TRUE(SetUserContainerProxy(list, work->id, std::nullopt));
  EXPECT_FALSE(ParseUserContainers(list)[0].proxy);
  EXPECT_FALSE(list[0].GetDict().contains("proxy"));

  // Not one of the user's containers.
  EXPECT_FALSE(SetUserContainerProxy(list, kHome, setting));
  EXPECT_FALSE(SetUserContainerProxy(
      list, {.kind = ContainerKind::kAgent, .id = work->id.id}, setting));
  EXPECT_FALSE(SetUserContainerProxy(
      list, {.kind = ContainerKind::kUser, .id = "twork"}, setting));
}

// What the New Profile dialog (koan_space_profile_menu.cc) refuses, and why:
// its Create is off while the name is empty, and the others get a message.
TEST(UserContainersTest, CheckNameSaysWhy) {
  using enum UserContainerNameError;
  EXPECT_EQ(kEmpty, CheckUserContainerName(u""));
  EXPECT_EQ(kEmpty, CheckUserContainerName(u"   "));
  EXPECT_EQ(kEmpty, CheckUserContainerName(u" \t\n "));
  EXPECT_EQ(kNone, CheckUserContainerName(u"Work"));
  EXPECT_EQ(kNone, CheckUserContainerName(u"  Work  "));
  // 64 characters is the most, white space at the ends not counted.
  EXPECT_EQ(kNone, CheckUserContainerName(std::u16string(64, u'x')));
  EXPECT_EQ(kNone,
            CheckUserContainerName(u"  " + std::u16string(64, u'x') + u"  "));
  EXPECT_EQ(kTooLong, CheckUserContainerName(std::u16string(65, u'x')));
  EXPECT_EQ(kControlCharacter, CheckUserContainerName(u"a\nb"));
  EXPECT_EQ(kControlCharacter, CheckUserContainerName(u"a\u0007b"));
}

// The rules of .agent/notes/upstream/space-containers.md, by number.
TEST(NewTabContainerTest, Rules) {
  const std::string work = "user:swork";
  const std::string home = "user:shome";

  // 6: a tab no page opened takes its Space's container, or the default.
  EXPECT_EQ(work, NewTabContainer({.space_container = work}));
  EXPECT_EQ("", NewTabContainer({}));

  // 1, 2: an opener, or a page no container holds: upstream.
  EXPECT_EQ(std::nullopt,
            NewTabContainer({.has_opener = true, .space_container = work}));
  EXPECT_EQ(std::nullopt,
            NewTabContainer({.container_url = false, .space_container = work}));

  // 3: a page's tab keeps its page's partition, also the default one in a
  // Space with a container.
  EXPECT_EQ(std::nullopt,
            NewTabContainer({.from_page = true, .space_container = work}));
  EXPECT_EQ(std::nullopt, NewTabContainer({.from_page = true,
                                           .source_container = home,
                                           .source_space_container = work,
                                           .space_container = work}));
  // ... unless a route sends it on from a tab in its Space's container.
  EXPECT_EQ(home, NewTabContainer({.from_page = true,
                                   .source_container = work,
                                   .source_space_container = work,
                                   .route_container = home,
                                   .space_container = work}));
  EXPECT_EQ("", NewTabContainer({.from_page = true,
                                 .source_container = work,
                                 .source_space_container = work,
                                 .route_container = "",
                                 .space_container = work}));
  EXPECT_EQ(std::nullopt, NewTabContainer({.from_page = true,
                                           .source_container = "",
                                           .source_space_container = work,
                                           .route_container = home}));
  EXPECT_EQ(std::nullopt, NewTabContainer({.from_page = true,
                                           .from_agent = true,
                                           .route_container = home}));

  // 5: a routed tab takes the routed Space's container, an extension's too.
  EXPECT_EQ(home, NewTabContainer(
                      {.route_container = home, .space_container = work}));
  EXPECT_EQ(home, NewTabContainer({.from_extension = true,
                                   .route_container = home,
                                   .space_container = work}));

  // 4: an extension's tab otherwise stays in the default.
  EXPECT_EQ(std::nullopt,
            NewTabContainer({.from_extension = true, .space_container = work}));
}

TEST(ContainerLineTest, OnlyAnotherUserContainerShows) {
  const ContainerId agent{.kind = ContainerKind::kAgent, .id = "tagent"};
  EXPECT_FALSE(ShowsContainerLine(std::nullopt, std::nullopt));
  EXPECT_FALSE(ShowsContainerLine(std::nullopt, kWork));
  EXPECT_FALSE(ShowsContainerLine(kWork, kWork));
  EXPECT_TRUE(ShowsContainerLine(kWork, std::nullopt));
  EXPECT_TRUE(ShowsContainerLine(kWork, kHome));
  EXPECT_FALSE(ShowsContainerLine(agent, std::nullopt));
}

TEST(EssentialsProfilesTest, OffShowsEveryEssentialInOneGrid) {
  EssentialsProfiles profiles;
  profiles.shown = kWork;
  profiles.spaces = {kWork, kHome};
  EXPECT_EQ(std::nullopt, profiles.GridOf(kHome));
  EXPECT_TRUE(profiles.Shows(kHome));
  EXPECT_TRUE(profiles.Shows(std::nullopt));
}

TEST(EssentialsProfilesTest, OnlyTheActiveSpacesProfileShows) {
  EssentialsProfiles profiles;
  profiles.separate = true;
  profiles.spaces = {kWork, kHome};
  // A Space with a profile shows its profile's Essentials only.
  profiles.shown = kWork;
  EXPECT_TRUE(profiles.Shows(kWork));
  EXPECT_FALSE(profiles.Shows(kHome));
  EXPECT_FALSE(profiles.Shows(std::nullopt));
  // A Space without one shows the Essentials without one.
  profiles.shown = std::nullopt;
  EXPECT_TRUE(profiles.Shows(std::nullopt));
  EXPECT_FALSE(profiles.Shows(kWork));
}

TEST(EssentialsProfilesTest, AProfileNoSpaceHasIsInTheDefaultGrid) {
  const ContainerId agent{.kind = ContainerKind::kAgent, .id = "tagent"};
  const ContainerId old{.kind = ContainerKind::kUser, .id = "sold"};
  EssentialsProfiles profiles;
  profiles.separate = true;
  profiles.spaces = {kWork};
  EXPECT_EQ(kWork, profiles.GridOf(kWork));
  EXPECT_EQ(std::nullopt, profiles.GridOf(old));
  EXPECT_EQ(std::nullopt, profiles.GridOf(agent));
  EXPECT_EQ(std::nullopt, profiles.GridOf(std::nullopt));
  EXPECT_TRUE(profiles.Shows(old));
  EXPECT_FALSE(profiles.Shows(kWork));
}

}  // namespace
}  // namespace koan::containers
