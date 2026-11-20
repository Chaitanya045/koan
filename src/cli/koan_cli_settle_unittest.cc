// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_settle.h"

#include <optional>
#include <string>
#include <utility>

#include "base/time/time.h"
#include "koan/cli/koan_cli_protocol.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::cli {
namespace {

using Probe = Settle::DomProbe;

constexpr char kTab[] = "T1";
constexpr char kFrame[] = "F1";

// A tab's event, as the facade sends it.
Message Event(std::string_view method, std::string_view params) {
  std::optional<Message> message =
      ParseMessage(R"({"method":")" + std::string(method) + R"(","params":)" +
                   std::string(params) + "}");
  EXPECT_TRUE(message) << method;
  return message ? std::move(*message) : Message();
}

class KoanCliSettleTest : public testing::Test {
 protected:
  base::TimeTicks At(int ms) const { return start_ + base::Milliseconds(ms); }

  // A DOM probe sent at `sent` that says quiet at `answered`.
  void QuietProbe(Settle& settle, int sent, int answered) {
    ASSERT_TRUE(settle.WantsDomProbe(At(sent)));
    settle.OnDomProbeSent(At(sent));
    EXPECT_FALSE(settle.WantsDomProbe(At(sent)));
    settle.OnDomProbe(Probe::kQuiet, At(answered));
  }

  void Request(Settle& settle,
               int ms,
               std::string_view id,
               std::string_view type = "Fetch") {
    settle.OnEvent(
        Event("Network.requestWillBeSent",
              R"({"requestId":")" + std::string(id) + R"(","type":")" +
                  std::string(type) + R"(","frameId":"F1","loaderId":"L1"})"),
        At(ms));
  }
  void Finished(Settle& settle, int ms, std::string_view id) {
    settle.OnEvent(Event("Network.loadingFinished",
                         R"({"requestId":")" + std::string(id) + R"("})"),
                   At(ms));
  }

  const SettleTimings timings_;
  const base::TimeTicks start_ = base::TimeTicks() + base::Seconds(100);
};

TEST_F(KoanCliSettleTest, AClickThatChangesNothingTakesAboutTheGrace) {
  Settle settle(timings_, start_, /*navigating=*/false, kTab, kFrame);
  EXPECT_EQ(settle.Check(At(0)), SettleState::kWaiting);
  QuietProbe(settle, 5, 255);
  // A navigation could still start.
  EXPECT_EQ(settle.Check(At(260)), SettleState::kWaiting);
  EXPECT_EQ(settle.NextCheck(At(260)), At(300));
  EXPECT_EQ(settle.Check(At(300)), SettleState::kSettled);
  EXPECT_EQ(SettledLine(settle, SettleState::kSettled), "ok");
}

TEST_F(KoanCliSettleTest, ASlowFetchAfterAClickIsWaitedFor) {
  Settle settle(timings_, start_, false, kTab, kFrame);
  QuietProbe(settle, 5, 255);
  // The page fetched at 50 ms: the probe began before it, so its quiet says
  // nothing about what the answer does to the page.
  Request(settle, 50, "R1");
  EXPECT_EQ(settle.in_flight(), 1u);
  EXPECT_EQ(settle.Check(At(400)), SettleState::kWaiting);
  EXPECT_FALSE(settle.WantsDomProbe(At(400)));
  Finished(settle, 1500, "R1");
  EXPECT_EQ(settle.Check(At(1500)), SettleState::kWaiting);
  // The answer is rendered: a new probe, from after it.
  QuietProbe(settle, 1501, 1760);
  EXPECT_EQ(settle.Check(At(1760)), SettleState::kSettled);
}

TEST_F(KoanCliSettleTest, NetworkQuietMeansNothingEndedJustNow) {
  Settle settle(timings_, start_, false, kTab, kFrame);
  Request(settle, 10, "R1");
  Finished(settle, 20, "R1");
  QuietProbe(settle, 21, 300);
  EXPECT_EQ(settle.Check(At(300)), SettleState::kSettled);

  Settle late(timings_, start_, false, kTab, kFrame);
  Request(late, 10, "R1");
  Finished(late, 400, "R1");
  QuietProbe(late, 401, 500);
  // 250 ms after the last request ended.
  EXPECT_EQ(late.Check(At(600)), SettleState::kWaiting);
  EXPECT_EQ(late.NextCheck(At(600)), At(650));
  EXPECT_EQ(late.Check(At(650)), SettleState::kSettled);
}

TEST_F(KoanCliSettleTest, LongLivedRequestsDoNotCountButAHungOneHitsTheCap) {
  Settle settle(timings_, start_, false, kTab, kFrame);
  Request(settle, 10, "WS", "WebSocket");
  Request(settle, 10, "ES", "EventSource");
  EXPECT_EQ(settle.in_flight(), 0u);
  QuietProbe(settle, 11, 300);
  EXPECT_EQ(settle.Check(At(300)), SettleState::kSettled);

  Settle hung(timings_, start_, false, kTab, kFrame);
  Request(hung, 10, "poll");
  EXPECT_EQ(hung.Check(At(2999)), SettleState::kWaiting);
  EXPECT_EQ(hung.Check(At(3000)), SettleState::kCapped);
  // Not a page that is still loading: plain ok.
  EXPECT_EQ(SettledLine(hung, SettleState::kCapped), "ok");
}

TEST_F(KoanCliSettleTest, AClickThatNavigatesWaitsForTheCommitAndTheLoad) {
  Settle settle(timings_, start_, false, kTab, kFrame);
  settle.OnEvent(Event("Page.frameStartedLoading", R"({"frameId":"F1"})"),
                 At(120));
  // A subframe's load is not the page's.
  settle.OnEvent(
      Event("Page.frameNavigated", R"({"frame":{"id":"F2","parentId":"F1"}})"),
      At(150));
  EXPECT_FALSE(settle.WantsDomProbe(At(200)));
  EXPECT_EQ(settle.Check(At(5000)), SettleState::kWaiting);
  settle.OnEvent(
      Event("Page.frameNavigated", R"({"frame":{"id":"F1","loaderId":"L2"}})"),
      At(6000));
  EXPECT_TRUE(settle.navigated());
  // The probe before the commit read the old page.
  EXPECT_TRUE(settle.WantsDomProbe(At(6000)));
  QuietProbe(settle, 6001, 6260);
  EXPECT_EQ(settle.Check(At(6300)), SettleState::kWaiting);
  settle.OnEvent(Event("Page.loadEventFired", R"({"timestamp":1})"), At(6400));
  EXPECT_EQ(settle.Check(At(6400)), SettleState::kSettled);
}

TEST_F(KoanCliSettleTest, ANavigationThatNeverCommitsTimesOut) {
  SettleTimings timings;
  timings.navigation_timeout = base::Seconds(15);
  Settle settle(timings, start_, /*navigating=*/true, kTab, kFrame);
  EXPECT_FALSE(settle.WantsDomProbe(At(10)));
  EXPECT_EQ(settle.Check(At(14999)), SettleState::kWaiting);
  EXPECT_EQ(settle.NextCheck(At(1000)), At(15000));
  EXPECT_EQ(settle.Check(At(15000)), SettleState::kTimedOut);
  EXPECT_EQ(NavigationTimeoutText("https://slow.test/", base::Seconds(15)),
            "Koan.timeout: https://slow.test/ did not answer within 15 s (the "
            "server is slow or down); Koan stopped loading it, and the tab "
            "shows what it showed before. Retry, navigate elsewhere, or "
            "allow longer with --timeout <seconds>");
  // After a click, the click was done: "ok", still loading.
  Settle click(timings, start_, false, kTab, kFrame);
  click.OnEvent(Event("Page.frameStartedLoading", R"({"frameId":"F1"})"),
                At(100));
  EXPECT_EQ(click.Check(At(15100)), SettleState::kTimedOut);
  EXPECT_EQ(SettledLine(click, SettleState::kTimedOut), "ok (still loading)");
}

TEST_F(KoanCliSettleTest, ANavigateGetsItsStatus) {
  Settle settle(timings_, start_, /*navigating=*/true, kTab, kFrame);
  settle.OnEvent(Event("Network.requestWillBeSent",
                       R"({"requestId":"L2","loaderId":"L2","type":"Document",
                           "frameId":"F1"})"),
                 At(5));
  settle.OnEvent(Event("Network.responseReceived",
                       R"({"requestId":"L2","type":"Document","frameId":"F1",
                           "response":{"status":504,
                                       "statusText":"Gateway Timeout"}})"),
                 At(80));
  // The navigate's own answer: it committed.
  settle.OnNavigateAnswered(At(90));
  settle.OnEvent(Event("Page.loadEventFired", "{}"), At(100));
  Finished(settle, 100, "L2");
  QuietProbe(settle, 101, 360);
  EXPECT_EQ(settle.Check(At(360)), SettleState::kSettled);
  EXPECT_EQ(settle.http_status(), 504);
  EXPECT_EQ(SettledLine(settle, SettleState::kSettled),
            "ok (HTTP 504 Gateway Timeout)");
  EXPECT_EQ(SettledLine(settle, SettleState::kSettled, "ok: selected \"Pro\""),
            "ok: selected \"Pro\" (HTTP 504 Gateway Timeout)");

  // A 200 says nothing; a slow load that hits the cap says so.
  Settle ok(timings_, start_, true, kTab, kFrame);
  ok.OnEvent(Event("Network.responseReceived",
                   R"({"type":"Document","frameId":"F1",
                       "response":{"status":200,"statusText":"OK"}})"),
             At(50));
  ok.OnNavigateAnswered(At(60));
  EXPECT_EQ(ok.Check(At(3059)), SettleState::kWaiting);
  EXPECT_EQ(ok.Check(At(3060)), SettleState::kCapped);
  EXPECT_EQ(ok.http_status(), 200);
  EXPECT_EQ(SettledLine(ok, SettleState::kCapped), "ok (still loading)");
}

TEST_F(KoanCliSettleTest, SameDocumentAndAbortedNavigations) {
  Settle spa(timings_, start_, false, kTab, kFrame);
  spa.OnEvent(Event("Page.navigatedWithinDocument",
                    R"({"frameId":"F1","url":"https://a.test/#2"})"),
              At(30));
  QuietProbe(spa, 31, 290);
  EXPECT_EQ(spa.Check(At(300)), SettleState::kSettled);

  // A 204 or a download: loading starts and stops, nothing commits.
  Settle none(timings_, start_, false, kTab, kFrame);
  none.OnEvent(Event("Page.frameStartedLoading", R"({"frameId":"F1"})"),
               At(30));
  none.OnEvent(Event("Page.frameStoppedLoading", R"({"frameId":"F1"})"),
               At(60));
  QuietProbe(none, 61, 320);
  EXPECT_EQ(none.Check(At(320)), SettleState::kSettled);
  EXPECT_FALSE(none.navigated());
}

TEST_F(KoanCliSettleTest, ThePageWaitingForTheUserStopsTheWait) {
  Settle dialog(timings_, start_, false, kTab, kFrame);
  dialog.OnEvent(Event("Page.javascriptDialogOpening",
                       R"({"type":"alert","message":"x"})"),
                 At(50));
  EXPECT_EQ(dialog.Check(At(50)), SettleState::kBlocked);
  EXPECT_NE(dialog.blocked_reason().find("Koan.userNeeded"), std::string::npos);

  // Another tab's pause is not this one's.
  Settle paused(timings_, start_, false, kTab, kFrame);
  paused.OnEvent(Event("Koan.paused", R"({"targetId":"T2","reason":"typing"})"),
                 At(50));
  EXPECT_EQ(paused.Check(At(50)), SettleState::kWaiting);
  paused.OnEvent(Event("Koan.paused", R"({"targetId":"T1","reason":"typing"})"),
                 At(60));
  EXPECT_EQ(paused.Check(At(60)), SettleState::kBlocked);
  EXPECT_NE(paused.blocked_reason().find("Koan.paused"), std::string::npos);

  Settle closed(timings_, start_, false, kTab, kFrame);
  closed.OnEvent(Event("Target.targetDestroyed", R"({"targetId":"T1"})"),
                 At(10));
  EXPECT_EQ(closed.Check(At(10)), SettleState::kBlocked);
}

TEST_F(KoanCliSettleTest, ProbesThatCannotRun) {
  // Its document went: another one, a little later.
  Settle changed(timings_, start_, false, kTab, kFrame);
  changed.OnDomProbeSent(At(5));
  changed.OnDomProbe(Probe::kPageChanged, At(10));
  EXPECT_FALSE(changed.WantsDomProbe(At(50)));
  EXPECT_TRUE(changed.WantsDomProbe(At(110)));

  // The page does not run it: the network and the grace decide.
  Settle failed(timings_, start_, false, kTab, kFrame);
  failed.OnDomProbeSent(At(5));
  failed.OnDomProbe(Probe::kFailed, At(10));
  EXPECT_FALSE(failed.WantsDomProbe(At(20)));
  EXPECT_EQ(failed.Check(At(300)), SettleState::kSettled);

  // A DOM that never stops changing: the cap.
  Settle busy(timings_, start_, false, kTab, kFrame);
  busy.OnDomProbeSent(At(5));
  EXPECT_EQ(busy.ProbeBudget(At(5)), base::Milliseconds(2995));
  busy.OnDomProbe(Probe::kBusy, At(3000));
  EXPECT_EQ(busy.Check(At(3000)), SettleState::kCapped);
}

TEST_F(KoanCliSettleTest, APageThatKeepsNavigatingStopsSomewhere) {
  Settle settle(timings_, start_, false, kTab, kFrame);
  for (int ms = 100; ms < 20000; ms += 1000) {
    settle.OnEvent(Event("Page.frameStartedLoading", R"({"frameId":"F1"})"),
                   At(ms));
    settle.OnEvent(Event("Page.frameNavigated", R"({"frame":{"id":"F1"}})"),
                   At(ms + 10));
  }
  EXPECT_EQ(settle.Check(At(18000)), SettleState::kCapped);
}

TEST(KoanCliSettleExpressionTest, TheProbeHasItsTimes) {
  const std::string probe =
      DomQuietExpression(base::Milliseconds(250), base::Milliseconds(2800));
  EXPECT_NE(probe.find("quietMs = 250, capMs = 2800"), std::string::npos);
  EXPECT_NE(probe.find("MutationObserver"), std::string::npos);
  // Never a cap of 0, which would end at once.
  EXPECT_NE(DomQuietExpression(base::Milliseconds(250), base::TimeDelta())
                .find("capMs = 1"),
            std::string::npos);
}

}  // namespace
}  // namespace koan::cli
