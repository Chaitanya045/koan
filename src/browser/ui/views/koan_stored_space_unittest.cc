// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_stored_space.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {
namespace {

std::vector<std::string> Spaces() {
  return {"a", "b", "c"};
}

TEST(KoanStoredSpaceTest, TheSpacesIdWinsOverThePlace) {
  // The Spaces were reordered (or one before it deleted) after the folder
  // was stored: its own Space, not the one now at its old place.
  const std::string id = "c";
  EXPECT_EQ(2u, StoredSpaceIndex(Spaces(), &id, 0));
}

TEST(KoanStoredSpaceTest, AnUnknownIdFallsBackToThePlace) {
  const std::string id = "gone";
  EXPECT_EQ(1u, StoredSpaceIndex(Spaces(), &id, 1));
  const std::string empty;
  EXPECT_EQ(1u, StoredSpaceIndex(Spaces(), &empty, 1));
  // An entry from before Space ids.
  EXPECT_EQ(0u, StoredSpaceIndex(Spaces(), nullptr, 0));
}

TEST(KoanStoredSpaceTest, APlacePastTheEndIsTheLastSpace) {
  EXPECT_EQ(2u, StoredSpaceIndex(Spaces(), nullptr, 7));
}

TEST(KoanStoredSpaceTest, NoSpaceForNoIdAndNoPlace) {
  const std::string id = "gone";
  EXPECT_EQ(std::nullopt, StoredSpaceIndex(Spaces(), &id, std::nullopt));
  EXPECT_EQ(std::nullopt, StoredSpaceIndex(Spaces(), nullptr, -1));
  EXPECT_EQ(std::nullopt, StoredSpaceIndex({}, &id, 0));
}

}  // namespace
}  // namespace koan
