// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_admission.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {

TEST(KoanAgentAdmissionTest, AtMostFourConnectionsThatHaveNotAuthenticated) {
  ConnectionAdmission admission;
  std::vector<ConnectionAdmission::Id> ids;
  for (size_t i = 0; i < ConnectionAdmission::kMaxUnauthenticated; ++i) {
    const auto id = admission.TryAdmit();
    ASSERT_NE(id, ConnectionAdmission::kRefused) << i;
    ids.push_back(id);
  }
  EXPECT_EQ(admission.TryAdmit(), ConnectionAdmission::kRefused);
  EXPECT_EQ(admission.open(), ConnectionAdmission::kMaxUnauthenticated);
  // A connection that closes makes room.
  admission.Release(ids[0]);
  EXPECT_NE(admission.TryAdmit(), ConnectionAdmission::kRefused);
}

TEST(KoanAgentAdmissionTest,
     AuthenticatedConnectionsFreeTheUnauthenticatedSlots) {
  ConnectionAdmission admission;
  std::vector<ConnectionAdmission::Id> ids;
  for (size_t i = 0; i < ConnectionAdmission::kMaxConnections; ++i) {
    const auto id = admission.TryAdmit();
    ASSERT_NE(id, ConnectionAdmission::kRefused) << i;
    ids.push_back(id);
    admission.MarkAuthenticated(id);  // each proves itself at once
  }
  // Eight in all is the most, authenticated or not.
  EXPECT_EQ(admission.open(), ConnectionAdmission::kMaxConnections);
  EXPECT_EQ(admission.unauthenticated(), 0u);
  EXPECT_EQ(admission.TryAdmit(), ConnectionAdmission::kRefused);
  admission.Release(ids[3]);
  EXPECT_NE(admission.TryAdmit(), ConnectionAdmission::kRefused);
}

TEST(KoanAgentAdmissionTest,
     SilentConnectionsDoNotCrowdOutAgentsThatAuthenticate) {
  ConnectionAdmission admission;
  std::vector<ConnectionAdmission::Id> ids;
  // Four connections that say nothing fill the unauthenticated slots...
  for (size_t i = 0; i < ConnectionAdmission::kMaxUnauthenticated; ++i) {
    ids.push_back(admission.TryAdmit());
    ASSERT_NE(ids.back(), ConnectionAdmission::kRefused);
  }
  EXPECT_EQ(admission.TryAdmit(), ConnectionAdmission::kRefused);
  // ...and a working agent that was among them proves itself, which makes room
  // for the next one without the silent ones getting any more.
  admission.MarkAuthenticated(ids[0]);
  EXPECT_EQ(admission.unauthenticated(),
            ConnectionAdmission::kMaxUnauthenticated - 1);
  EXPECT_NE(admission.TryAdmit(), ConnectionAdmission::kRefused);
  EXPECT_EQ(admission.TryAdmit(), ConnectionAdmission::kRefused);
  EXPECT_EQ(admission.open(), ConnectionAdmission::kMaxUnauthenticated + 1);
}

TEST(KoanAgentAdmissionTest, IdsAreNeverReusedAndUnknownIdsAreIgnored) {
  ConnectionAdmission admission;
  const auto first = admission.TryAdmit();
  admission.Release(first);
  const auto second = admission.TryAdmit();
  EXPECT_NE(first, second);
  admission.Release(first);  // already gone
  admission.MarkAuthenticated(first);
  admission.Release(ConnectionAdmission::kRefused);
  EXPECT_EQ(admission.open(), 1u);
  EXPECT_EQ(admission.unauthenticated(), 1u);
}

TEST(KoanAgentAdmissionTest, OneProgramMayHoldOnlyTwoUnauthenticatedSlots) {
  ConnectionAdmission admission;
  std::vector<ConnectionAdmission::Id> ids;
  for (size_t i = 0; i < ConnectionAdmission::kMaxUnauthenticated; ++i) {
    ids.push_back(admission.TryAdmit());
  }
  EXPECT_TRUE(admission.AssignPeer(ids[0], "a"));
  EXPECT_TRUE(admission.AssignPeer(ids[1], "a"));
  // The third from the same program is refused, and does not count.
  EXPECT_FALSE(admission.AssignPeer(ids[2], "a"));
  EXPECT_EQ(admission.unauthenticated_for_peer("a"), 2u);
  // Another program is not affected.
  EXPECT_TRUE(admission.AssignPeer(ids[3], "b"));
  // A slot comes back when the program authenticates or closes one.
  admission.Release(ids[2]);
  admission.MarkAuthenticated(ids[0]);
  const auto next = admission.TryAdmit();
  ASSERT_NE(next, ConnectionAdmission::kRefused);
  EXPECT_TRUE(admission.AssignPeer(next, "a"));
  EXPECT_EQ(admission.unauthenticated_for_peer("a"), 2u);
  // An unknown id is ignored.
  EXPECT_TRUE(admission.AssignPeer(9999, "a"));
}

}  // namespace koan::agent
