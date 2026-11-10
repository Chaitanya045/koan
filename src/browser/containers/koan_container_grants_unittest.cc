// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_container_grants.h"

#include <map>
#include <set>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan::containers {
namespace {

using Kind = ContainerChoice::Kind;

constexpr char kAlice[] = "0123456789abcdef0123456789abcdef";
constexpr char kBob[] = "fedcba9876543210fedcba9876543210";

using Saved = std::map<std::string, std::string, std::less<>>;
using InUse = std::set<std::string, std::less<>>;

Saved TwoSaved() {
  return {{"GitHub bot", "sgithub"}, {"Mail", "smail"}};
}

TEST(ContainerGrantsTest, NoRequestIsThrowaway) {
  ContainerGrants grants;
  grants.Set({.id = kAlice}, "sgithub", true);
  EXPECT_EQ(ChooseContainer(grants, kAlice, "", TwoSaved(), InUse()).kind,
            Kind::kThrowaway);
}

// The review finding of #202: naming a saved container is never enough.
TEST(ContainerGrantsTest, NamingWithoutAGrantIsRefused) {
  ContainerGrants grants;
  EXPECT_EQ(
      ChooseContainer(grants, kAlice, "GitHub bot", TwoSaved(), InUse()).kind,
      Kind::kRefused);
  grants.Set({.id = kBob}, "sgithub", true);
  EXPECT_EQ(
      ChooseContainer(grants, kAlice, "GitHub bot", TwoSaved(), InUse()).kind,
      Kind::kRefused);
  // A grant for one container isn't one for another.
  grants.Set({.id = kAlice}, "smail", true);
  EXPECT_EQ(
      ChooseContainer(grants, kAlice, "GitHub bot", TwoSaved(), InUse()).kind,
      Kind::kRefused);
}

TEST(ContainerGrantsTest, GrantedAndNamedIsSaved) {
  ContainerGrants grants;
  grants.Set({.id = kAlice, .label = "Claude Code"}, "sgithub", true);
  EXPECT_EQ(ChooseContainer(grants, kAlice, "GitHub bot", TwoSaved(), InUse()),
            (ContainerChoice{.kind = Kind::kSaved, .container_id = "sgithub"}));
  // Names match exactly.
  EXPECT_EQ(
      ChooseContainer(grants, kAlice, "github bot", TwoSaved(), InUse()).kind,
      Kind::kRefused);
}

TEST(ContainerGrantsTest, AnUnknownNameNeverCreatesOne) {
  ContainerGrants grants;
  grants.Set({.id = kAlice}, "snew", true);
  EXPECT_EQ(ChooseContainer(grants, kAlice, "New", TwoSaved(), InUse()).kind,
            Kind::kRefused);
}

TEST(ContainerGrantsTest, AContainerInUseByAnotherTaskIsRefused) {
  ContainerGrants grants;
  grants.Set({.id = kAlice}, "sgithub", true);
  EXPECT_EQ(
      ChooseContainer(grants, kAlice, "GitHub bot", TwoSaved(), {"sgithub"})
          .kind,
      Kind::kRefused);
}

TEST(ContainerGrantsTest, RevokeAndDelete) {
  ContainerGrants grants;
  grants.Set({.id = kAlice, .label = "A"}, "sgithub", true);
  grants.Set({.id = kAlice, .label = "A"}, "smail", true);
  grants.Set({.id = kBob, .label = "B"}, "sgithub", true);
  grants.Set({.id = kAlice}, "smail", false);
  EXPECT_FALSE(grants.IsGranted(kAlice, "smail"));
  EXPECT_TRUE(grants.IsGranted(kAlice, "sgithub"));

  // Deleting a container drops its grants, and a client with none left.
  grants.RemoveContainer("sgithub");
  EXPECT_FALSE(grants.IsGranted(kAlice, "sgithub"));
  EXPECT_FALSE(grants.IsGranted(kBob, "sgithub"));
  EXPECT_TRUE(grants.All().empty());
  EXPECT_TRUE(grants.Clients().empty());
}

TEST(ContainerGrantsTest, EmptyIdsAreNeverGranted) {
  ContainerGrants grants;
  grants.Set({.id = ""}, "sgithub", true);
  grants.Set({.id = kAlice}, "", true);
  EXPECT_TRUE(grants.All().empty());
  EXPECT_FALSE(grants.IsGranted("", "sgithub"));
}

TEST(ContainerGrantsTest, ClientsKeepTheirLabel) {
  ContainerGrants grants;
  grants.Set({.id = kAlice, .label = "Claude Code"}, "sgithub", true);
  ASSERT_EQ(grants.Clients().size(), 1u);
  EXPECT_EQ(grants.Clients()[0].label, "Claude Code");
  grants.Set({.id = kAlice}, "sgithub", false);
  EXPECT_TRUE(grants.Clients().empty());
}

}  // namespace
}  // namespace koan::containers
