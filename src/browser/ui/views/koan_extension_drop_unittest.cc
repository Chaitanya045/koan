// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_extension_drop.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan {
namespace {

TEST(KoanExtensionDropTest, SlotUnderThePointer) {
  EXPECT_EQ(ExtensionDropSlot(0, 28, 3), 0u);
  EXPECT_EQ(ExtensionDropSlot(27, 28, 3), 0u);
  EXPECT_EQ(ExtensionDropSlot(28, 28, 3), 1u);
  EXPECT_EQ(ExtensionDropSlot(83, 28, 3), 2u);
}

TEST(KoanExtensionDropTest, SlotIsClampedToTheRow) {
  EXPECT_EQ(ExtensionDropSlot(-40, 28, 3), 0u);
  EXPECT_EQ(ExtensionDropSlot(500, 28, 3), 2u);
  EXPECT_EQ(ExtensionDropSlot(500, 28, 1), 0u);
  EXPECT_EQ(ExtensionDropSlot(10, 0, 3), 0u);
}

TEST(KoanExtensionDropTest, OrderMovesTheDraggedButton) {
  EXPECT_EQ(ExtensionDropOrder(4, 0, 2), (std::vector<size_t>{1, 2, 0, 3}));
  EXPECT_EQ(ExtensionDropOrder(4, 3, 0), (std::vector<size_t>{3, 0, 1, 2}));
  EXPECT_EQ(ExtensionDropOrder(4, 1, 1), (std::vector<size_t>{0, 1, 2, 3}));
  EXPECT_EQ(ExtensionDropOrder(1, 0, 0), (std::vector<size_t>{0}));
}

}  // namespace
}  // namespace koan
