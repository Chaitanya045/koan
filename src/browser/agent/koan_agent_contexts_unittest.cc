// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_contexts.h"

#include <string>
#include <string_view>
#include <vector>

#include "base/strings/stringprintf.h"
#include "base/test/values_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {

namespace {

base::DictValue Created(int id, std::string_view type, bool is_default) {
  return base::test::ParseJsonDict(base::StringPrintf(
      R"({"context":{"id":%d,"uniqueId":"u%d","name":"w","auxData":)"
      R"({"isDefault":%s,"type":"%s","frameId":"F"}}})",
      id, id, is_default ? "true" : "false", std::string(type).c_str()));
}

base::DictValue Console(int id) {
  return base::test::ParseJsonDict(base::StringPrintf(
      R"({"type":"log","args":[],"executionContextId":%d})", id));
}

std::vector<std::string> Methods(const std::vector<ContextEvent>& events) {
  std::vector<std::string> methods;
  for (const ContextEvent& event : events) {
    methods.push_back(event.method);
  }
  return methods;
}

}  // namespace

TEST(KoanAgentContextsTest, MainWorldsAreDeliveredAndNamable) {
  ContextTracker tracker;
  EXPECT_EQ(tracker
                .OnEvent("Runtime.executionContextCreated",
                         Created(1, "default", true))
                .size(),
            1u);
  EXPECT_TRUE(tracker.IsAllowedContextId(1));
  EXPECT_TRUE(tracker.IsAllowedUniqueId("u1"));
  EXPECT_EQ(tracker.OnEvent("Runtime.consoleAPICalled", Console(1)).size(), 1u);
}

// An extension's content script runs in an isolated world the agent did not
// make: nothing of it reaches the agent (design 4.4, T-sec-11).
TEST(KoanAgentContextsTest, OtherWorldsAreInvisible) {
  ContextTracker tracker;
  EXPECT_TRUE(tracker
                  .OnEvent("Runtime.executionContextCreated",
                           Created(5, "isolated", false))
                  .empty());
  EXPECT_FALSE(tracker.IsAllowedContextId(5));
  EXPECT_FALSE(tracker.IsAllowedUniqueId("u5"));
  EXPECT_TRUE(tracker.OnEvent("Runtime.consoleAPICalled", Console(5)).empty());
  EXPECT_TRUE(
      tracker
          .OnEvent("Runtime.exceptionThrown",
                   base::test::ParseJsonDict(
                       R"({"exceptionDetails":{"executionContextId":5}})"))
          .empty());
  EXPECT_TRUE(tracker
                  .OnEvent("Runtime.bindingCalled",
                           base::test::ParseJsonDict(
                               R"({"name":"b","executionContextId":5})"))
                  .empty());
  // isDefault without the default type, and the reverse, are not a main world.
  EXPECT_TRUE(tracker
                  .OnEvent("Runtime.executionContextCreated",
                           Created(6, "isolated", true))
                  .empty());
  EXPECT_TRUE(tracker
                  .OnEvent("Runtime.executionContextCreated",
                           Created(7, "default", false))
                  .empty());
}

TEST(KoanAgentContextsTest, EventsWithoutAContextAreDropped) {
  ContextTracker tracker;
  EXPECT_TRUE(
      tracker
          .OnEvent("Runtime.exceptionThrown",
                   base::test::ParseJsonDict(R"({"exceptionDetails":{}})"))
          .empty());
  EXPECT_TRUE(tracker
                  .OnEvent("Runtime.executionContextCreated",
                           base::test::ParseJsonDict(R"({"context":{}})"))
                  .empty());
}

// The created event comes before the answer; the answer decides.
TEST(KoanAgentContextsTest, TheAgentsWorldIsHeldUntilTheAnswer) {
  ContextTracker tracker;
  tracker.OnIsolatedWorldRequested();
  // The agent's world, and an extension's that happens to start meanwhile.
  EXPECT_TRUE(tracker
                  .OnEvent("Runtime.executionContextCreated",
                           Created(8, "isolated", false))
                  .empty());
  EXPECT_TRUE(tracker
                  .OnEvent("Runtime.executionContextCreated",
                           Created(9, "isolated", false))
                  .empty());
  EXPECT_TRUE(tracker.OnEvent("Runtime.consoleAPICalled", Console(9)).empty());
  EXPECT_TRUE(tracker.OnEvent("Runtime.consoleAPICalled", Console(8)).empty());
  EXPECT_FALSE(tracker.IsAllowedContextId(8));

  const std::vector<ContextEvent> released = tracker.OnIsolatedWorldAnswered(8);
  EXPECT_EQ(Methods(released),
            (std::vector<std::string>{"Runtime.executionContextCreated",
                                      "Runtime.consoleAPICalled"}));
  EXPECT_EQ(released[0].params.FindDict("context")->FindInt("id"), 8);
  EXPECT_TRUE(tracker.IsAllowedContextId(8));
  EXPECT_TRUE(tracker.IsAllowedUniqueId("u8"));
  EXPECT_FALSE(tracker.IsAllowedContextId(9));
  EXPECT_EQ(tracker.held_count_for_testing(), 0u);
  EXPECT_TRUE(tracker.OnEvent("Runtime.consoleAPICalled", Console(9)).empty());
}

TEST(KoanAgentContextsTest, AFailedCreationReleasesNothing) {
  ContextTracker tracker;
  tracker.OnIsolatedWorldRequested();
  tracker.OnEvent("Runtime.executionContextCreated",
                  Created(8, "isolated", false));
  EXPECT_TRUE(tracker.OnIsolatedWorldAnswered(std::nullopt).empty());
  EXPECT_FALSE(tracker.IsAllowedContextId(8));
  EXPECT_EQ(tracker.held_count_for_testing(), 0u);
}

TEST(KoanAgentContextsTest, TwoCreationsInFlight) {
  ContextTracker tracker;
  tracker.OnIsolatedWorldRequested();
  tracker.OnIsolatedWorldRequested();
  tracker.OnEvent("Runtime.executionContextCreated",
                  Created(10, "isolated", false));
  tracker.OnEvent("Runtime.executionContextCreated",
                  Created(11, "isolated", false));
  EXPECT_EQ(tracker.OnIsolatedWorldAnswered(10).size(), 1u);
  // The second is still in flight: 11 stays held.
  EXPECT_EQ(tracker.held_count_for_testing(), 1u);
  EXPECT_EQ(tracker.OnIsolatedWorldAnswered(11).size(), 1u);
  EXPECT_TRUE(tracker.IsAllowedContextId(10));
  EXPECT_TRUE(tracker.IsAllowedContextId(11));
}

// Without Runtime.enable there are no events; the answer alone allows the id.
TEST(KoanAgentContextsTest, TheAnswerAllowsWithoutEvents) {
  ContextTracker tracker;
  tracker.OnIsolatedWorldRequested();
  EXPECT_TRUE(tracker.OnIsolatedWorldAnswered(12).empty());
  EXPECT_TRUE(tracker.IsAllowedContextId(12));
  // Reported later by Runtime.enable: delivered.
  EXPECT_EQ(tracker
                .OnEvent("Runtime.executionContextCreated",
                         Created(12, "isolated", false))
                .size(),
            1u);
}

TEST(KoanAgentContextsTest, IsolatedWorldsAreDroppedWhenNothingIsInFlight) {
  ContextTracker tracker;
  EXPECT_TRUE(tracker
                  .OnEvent("Runtime.executionContextCreated",
                           Created(13, "isolated", false))
                  .empty());
  EXPECT_EQ(tracker.held_count_for_testing(), 0u);
  // A late answer for an unrelated id does not resurrect it.
  tracker.OnIsolatedWorldRequested();
  tracker.OnIsolatedWorldAnswered(14);
  EXPECT_FALSE(tracker.IsAllowedContextId(13));
}

TEST(KoanAgentContextsTest, DestroyedAndClearedForget) {
  ContextTracker tracker;
  tracker.OnEvent("Runtime.executionContextCreated",
                  Created(1, "default", true));
  tracker.OnEvent("Runtime.executionContextCreated",
                  Created(2, "default", true));
  EXPECT_EQ(tracker
                .OnEvent("Runtime.executionContextDestroyed",
                         base::test::ParseJsonDict(
                             R"({"executionContextId":1,)"
                             R"("executionContextUniqueId":"u1"})"))
                .size(),
            1u);
  EXPECT_FALSE(tracker.IsAllowedContextId(1));
  EXPECT_FALSE(tracker.IsAllowedUniqueId("u1"));
  // Another world's destruction is not delivered either.
  EXPECT_TRUE(
      tracker
          .OnEvent("Runtime.executionContextDestroyed",
                   base::test::ParseJsonDict(R"({"executionContextId":40})"))
          .empty());
  EXPECT_EQ(
      tracker.OnEvent("Runtime.executionContextsCleared", base::DictValue())
          .size(),
      1u);
  EXPECT_FALSE(tracker.IsAllowedContextId(2));
}

TEST(KoanAgentContextsTest, AWorldDestroyedWhileHeldIsNotAllowedAfter) {
  ContextTracker tracker;
  tracker.OnIsolatedWorldRequested();
  tracker.OnEvent("Runtime.executionContextCreated",
                  Created(8, "isolated", false));
  tracker.OnEvent("Runtime.executionContextDestroyed",
                  base::test::ParseJsonDict(R"({"executionContextId":8})"));
  EXPECT_EQ(tracker.OnIsolatedWorldAnswered(8).size(), 2u);
  EXPECT_FALSE(tracker.IsAllowedContextId(8));
}

TEST(KoanAgentContextsTest, HeldEventsAreBounded) {
  ContextTracker tracker;
  tracker.OnIsolatedWorldRequested();
  tracker.OnEvent("Runtime.executionContextCreated",
                  Created(8, "isolated", false));
  for (size_t i = 0; i < ContextTracker::kMaxHeldEvents + 10; ++i) {
    tracker.OnEvent("Runtime.consoleAPICalled", Console(8));
  }
  EXPECT_EQ(tracker.held_count_for_testing(), ContextTracker::kMaxHeldEvents);
}

}  // namespace koan::agent
