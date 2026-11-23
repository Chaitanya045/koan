// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_indicator_logic.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {
namespace {

std::vector<IndicatorClient> Claude() {
  return {{"c1", "Claude Code"}};
}

IndicatorTab Tab(std::string id,
                 bool agent_owned,
                 TabState state,
                 bool shown,
                 bool in_window = true) {
  IndicatorTab tab;
  tab.target_id = std::move(id);
  tab.label = "Claude Code";
  tab.title = u"Page";
  tab.agent_owned = agent_owned;
  tab.state = state;
  tab.in_window = in_window;
  tab.shown = shown;
  return tab;
}

IndicatorPrompt Prompt(uint64_t id, uintptr_t tab, bool shown) {
  IndicatorPrompt prompt;
  prompt.prompt.id = id;
  prompt.prompt.tab = tab;
  prompt.prompt.surface = PageSurface::kHttpAuth;
  prompt.prompt.host = "a.test";
  prompt.shown = shown;
  return prompt;
}

TEST(KoanAgentIndicatorLogicTest, NothingWhileNoAgentIsConnected) {
  const IndicatorContent content = IndicatorFor(
      {}, {Tab("t", false, TabState::kDriven, true)}, {Prompt(1, 1, true)});
  EXPECT_FALSE(content.connected);
  EXPECT_FALSE(content.pill);
  EXPECT_EQ(content.chip, ChipContent());
}

TEST(KoanAgentIndicatorLogicTest, ChipSaysWhoIsConnected) {
  EXPECT_EQ(IndicatorFor(Claude(), {}, {}).chip.accessible_name,
            u"Claude Code is connected");
  EXPECT_EQ(IndicatorFor({{"c1", ""}}, {}, {}).chip.accessible_name,
            u"An agent is connected");
  // The same client on two connections is one agent.
  const IndicatorContent twice =
      IndicatorFor({{"c1", "Claude Code"}, {"c1", "Claude Code"}}, {}, {});
  EXPECT_EQ(twice.chip.text, u"Claude Code");
  EXPECT_EQ(twice.chip.accessible_name, u"Claude Code is connected");
  EXPECT_EQ(IndicatorFor({{"c1", "Claude Code"}, {"c2", "Codex"}}, {}, {})
                .chip.accessible_name,
            u"2 agents connected");
}

// The tooltip says while Koan keeps the display awake for an agent; the chip's
// text does not change, and with no agent there is nothing to say.
TEST(KoanAgentIndicatorLogicTest, ChipSaysWhenTheDisplayIsKeptAwake) {
  const IndicatorContent awake = IndicatorFor(Claude(), {}, {}, true);
  EXPECT_EQ(awake.chip.accessible_name,
            u"Claude Code is connected; keeping the display awake");
  EXPECT_EQ(awake.chip.text, IndicatorFor(Claude(), {}, {}).chip.text);
  EXPECT_EQ(IndicatorFor({}, {}, {}, true).chip, ChipContent());
}

// The user's complaint: the pill showed on every tab. A tab no agent holds
// gets the chip only.
TEST(KoanAgentIndicatorLogicTest, AnotherTabShowsTheChipNotThePill) {
  const IndicatorContent content = IndicatorFor(
      Claude(), {Tab("t", false, TabState::kDriven, /*shown=*/false)}, {});
  EXPECT_TRUE(content.connected);
  EXPECT_FALSE(content.pill);
  EXPECT_EQ(content.chip.text, u"Claude Code");
  EXPECT_EQ(content.chip.accessible_name, u"Claude Code is connected");
  EXPECT_EQ(content.chip.waiting, 0u);
}

TEST(KoanAgentIndicatorLogicTest, ChipWithoutAnyAgentTab) {
  const IndicatorContent content = IndicatorFor(Claude(), {}, {});
  EXPECT_TRUE(content.connected);
  EXPECT_FALSE(content.pill);
  EXPECT_EQ(content.chip.text, u"Claude Code");
  EXPECT_EQ(IndicatorFor({{"c1", "A"}, {"c2", "B"}}, {}, {}).chip.text,
            u"2 agents");
  EXPECT_EQ(IndicatorFor({{"c1", ""}}, {}, {}).chip.text, u"Agent");
}

TEST(KoanAgentIndicatorLogicTest, TakenOverTabShowsThePill) {
  const IndicatorContent content =
      IndicatorFor(Claude(), {Tab("t", false, TabState::kDriven, true)}, {});
  ASSERT_TRUE(content.pill);
  EXPECT_EQ(content.pill->text, u"● Claude Code is controlling this tab");
  EXPECT_EQ(content.pill->target_id, "t");
  EXPECT_TRUE(content.pill->take_back);
  EXPECT_FALSE(content.pill->hand_back);
  EXPECT_FALSE(content.pill->take_over);
  EXPECT_EQ(content.pill->prompt_id, 0u);
}

TEST(KoanAgentIndicatorLogicTest, ReadableUserTab) {
  const IndicatorContent content =
      IndicatorFor(Claude(), {Tab("t", false, TabState::kReadable, true)}, {});
  ASSERT_TRUE(content.pill);
  EXPECT_EQ(content.pill->text, u"● Claude Code can read this tab");
  EXPECT_TRUE(content.pill->take_back);
}

TEST(KoanAgentIndicatorLogicTest, AgentTabTheUserLooksAt) {
  const IndicatorContent content =
      IndicatorFor(Claude(), {Tab("a", true, TabState::kDriven, true)}, {});
  ASSERT_TRUE(content.pill);
  EXPECT_EQ(content.pill->text, u"● Claude Code is controlling this tab");
  EXPECT_TRUE(content.pill->take_over);
  EXPECT_FALSE(content.pill->take_back);
}

TEST(KoanAgentIndicatorLogicTest, PausedTabs) {
  IndicatorContent content =
      IndicatorFor(Claude(), {Tab("t", false, TabState::kPaused, true)}, {});
  ASSERT_TRUE(content.pill);
  EXPECT_EQ(content.pill->text, u"● Claude Code paused: you used this tab");
  EXPECT_TRUE(content.pill->hand_back);
  EXPECT_TRUE(content.pill->take_back);
  EXPECT_FALSE(content.pill->take_over);

  IndicatorTab asks = Tab("a", true, TabState::kPaused, true);
  asks.resume_asked = true;
  content = IndicatorFor(Claude(), {asks}, {});
  ASSERT_TRUE(content.pill);
  EXPECT_EQ(content.pill->text, u"● Claude Code asks to continue in this tab");
  EXPECT_TRUE(content.pill->hand_back);
  // An agent's own tab is never taken back: it stays the agent's.
  EXPECT_FALSE(content.pill->take_back);
}

// Auto-resume (design 9.2): a pause that resumes by itself offers Keep
// paused, and counts down while the user is on the tab and idle.
TEST(KoanAgentIndicatorLogicTest, AutoResumingPauseCountsDown) {
  IndicatorTab tab = Tab("a", true, TabState::kPaused, true);
  tab.auto_resumes = true;
  IndicatorContent content = IndicatorFor(Claude(), {tab}, {});
  ASSERT_TRUE(content.pill);
  EXPECT_EQ(content.pill->text,
            u"● Claude Code paused: continues when you stop using this tab");
  EXPECT_TRUE(content.pill->keep_paused);
  EXPECT_TRUE(content.pill->hand_back);
  EXPECT_FALSE(content.pill->countdown_seconds);

  tab.countdown_seconds = 5;
  content = IndicatorFor(Claude(), {tab}, {});
  EXPECT_EQ(content.pill->text, u"● Claude Code continues in 5s");
  EXPECT_EQ(content.pill->countdown_seconds, 5);
  EXPECT_TRUE(content.pill->keep_paused);
  EXPECT_TRUE(content.pill->hand_back);
  tab.countdown_seconds = 0;
  EXPECT_EQ(IndicatorFor(Claude(), {tab}, {}).pill->text,
            u"● Claude Code continues in 1s");

  // Held (Keep paused, a password field, ...): no countdown, no Keep paused.
  tab.auto_resumes = false;
  content = IndicatorFor(Claude(), {tab}, {});
  EXPECT_EQ(content.pill->text, u"● Claude Code paused: you used this tab");
  EXPECT_FALSE(content.pill->keep_paused);
  EXPECT_FALSE(content.pill->countdown_seconds);
  EXPECT_TRUE(content.pill->hand_back);
}

TEST(KoanAgentIndicatorLogicTest, InASplitThePausedPaneWins) {
  const IndicatorContent content =
      IndicatorFor(Claude(),
                   {Tab("a", true, TabState::kDriven, true),
                    Tab("b", false, TabState::kPaused, true)},
                   {});
  ASSERT_TRUE(content.pill);
  EXPECT_EQ(content.pill->target_id, "b");
}

TEST(KoanAgentIndicatorLogicTest, ATabOfAnotherWindowIsNotShownHere) {
  // `shown` in its own window, not in this one.
  const IndicatorContent content = IndicatorFor(
      Claude(), {Tab("t", false, TabState::kDriven, true, /*in_window=*/false)},
      {});
  EXPECT_FALSE(content.pill);
}

TEST(KoanAgentIndicatorLogicTest, PausedTabElsewhereWaits) {
  const IndicatorContent content =
      IndicatorFor(Claude(), {Tab("t", false, TabState::kPaused, false)}, {});
  EXPECT_FALSE(content.pill);
  EXPECT_EQ(content.chip.waiting, 1u);
  EXPECT_EQ(content.chip.text, u"Claude Code · 1 waiting");
  EXPECT_EQ(content.chip.accessible_name,
            u"Claude Code is connected; 1 tab waits for you");
}

TEST(KoanAgentIndicatorLogicTest, PromptInTheShownTabTakesThePill) {
  const IndicatorContent content =
      IndicatorFor(Claude(), {Tab("a", true, TabState::kDriven, true)},
                   {Prompt(7, 1, true), Prompt(8, 1, true)});
  ASSERT_TRUE(content.pill);
  EXPECT_EQ(content.pill->prompt_id, 7u);
  EXPECT_EQ(content.pill->text, u"● a.test asks you to sign in (+1)");
  EXPECT_EQ(content.chip.waiting, 0u);
}

TEST(KoanAgentIndicatorLogicTest, PromptElsewhereIsOnlyCountedOnTheChip) {
  const IndicatorContent content = IndicatorFor(
      Claude(), {Tab("a", true, TabState::kDriven, false)},
      {Prompt(7, 1, false), Prompt(8, 1, false), Prompt(9, 2, false)});
  EXPECT_FALSE(content.pill);
  // Two tabs wait (prompts are counted per tab).
  EXPECT_EQ(content.chip.waiting, 2u);
  EXPECT_EQ(content.chip.text, u"Claude Code · 2 waiting");
  EXPECT_EQ(content.chip.accessible_name,
            u"Claude Code is connected; 2 tabs wait for you");
}

TEST(KoanAgentIndicatorLogicTest, MenuGoesToWhatIsNotShown) {
  IndicatorTab other = Tab("b", false, TabState::kDriven, false);
  other.title = u"Mail";
  IndicatorTab untitled = Tab("c", true, TabState::kDriven, false, false);
  untitled.title.clear();
  const std::vector<MenuEntry> entries =
      MenuFor({Tab("a", true, TabState::kDriven, true), other,
               Tab("p", true, TabState::kPaused, false), untitled},
              {Prompt(7, 1, false), Prompt(8, 1, false), Prompt(9, 2, true)});
  ASSERT_EQ(entries.size(), 4u);
  // One prompt per tab, the shown tab's not at all.
  EXPECT_EQ(entries[0], (MenuEntry{.label = u"a.test asks you to sign in",
                                   .prompt_id = 7}));
  // Then paused tabs, then the others; the shown tab is left out.
  EXPECT_EQ(entries[1], (MenuEntry{.label = u"Go to Page (Claude Code, paused)",
                                   .target_id = "p"}));
  EXPECT_EQ(entries[2], (MenuEntry{.label = u"Go to Mail (Claude Code)",
                                   .target_id = "b"}));
  // A tab of another window is listed (`shown` there doesn't hide it here).
  EXPECT_EQ(entries[3], (MenuEntry{.label = u"Go to Untitled (Claude Code)",
                                   .target_id = "c"}));
}

TEST(KoanAgentIndicatorLogicTest, EventToasts) {
  const EventState none;
  const EventState connected = EventStateFor(Claude(), {});
  EXPECT_EQ(EventToastsFor(none, connected).connection,
            u"Claude Code connected");
  EXPECT_EQ(EventToastsFor(connected, none).connection,
            u"Claude Code disconnected");
  EXPECT_EQ(EventToastsFor(connected, connected).connection, u"");
  EXPECT_EQ(EventToastsFor(none, EventStateFor({{"c1", ""}}, {})).connection,
            u"An agent connected");
  // A second connection of the same client is no event.
  EXPECT_EQ(
      EventToastsFor(
          connected,
          EventStateFor({{"c1", "Claude Code"}, {"c1", "Claude Code"}}, {}))
          .connection,
      u"");

  const EventState driving =
      EventStateFor(Claude(), {Tab("t", false, TabState::kDriven, true)});
  const EventState paused =
      EventStateFor(Claude(), {Tab("t", false, TabState::kPaused, true)});
  EXPECT_EQ(EventToastsFor(driving, paused).pause,
            u"Claude Code paused: you used its tab");
  EXPECT_EQ(EventToastsFor(driving, paused).connection, u"");
  EXPECT_EQ(EventToastsFor(paused, paused).pause, u"");
  EXPECT_EQ(EventToastsFor(paused, driving).pause, u"");
  // A tab paused in another window toasts there, not here.
  EXPECT_EQ(
      EventToastsFor(driving,
                     EventStateFor(Claude(), {Tab("t", false, TabState::kPaused,
                                                  true, /*in_window=*/false)}))
          .pause,
      u"");
}

// `koan upload`: for a few seconds the pill on the tab says what the agent
// attached, and its window gets a toast once per upload.
TEST(KoanAgentIndicatorLogicTest, WhatAnAgentAttached) {
  IndicatorTab tab = Tab("t", true, TabState::kDriven, true);
  tab.attached = u"resume.pdf";
  tab.attached_seq = 4;
  const IndicatorContent content = IndicatorFor(Claude(), {tab}, {});
  ASSERT_TRUE(content.pill);
  EXPECT_EQ(content.pill->text, u"● Claude Code attached resume.pdf");
  EXPECT_TRUE(content.pill->take_over);
  // Paused, the pause says more.
  IndicatorTab paused = tab;
  paused.state = TabState::kPaused;
  EXPECT_EQ(IndicatorFor(Claude(), {paused}, {}).pill->text,
            u"● Claude Code paused: you used this tab");

  const EventState before = EventStateFor(Claude(), {});
  const EventState after = EventStateFor(Claude(), {tab});
  EXPECT_EQ(EventToastsFor(before, after).upload,
            u"Claude Code attached resume.pdf");
  // Once per upload: not again while it is shown, again for the next one.
  EXPECT_EQ(EventToastsFor(after, after).upload, u"");
  IndicatorTab next = tab;
  next.attached_seq = 5;
  EXPECT_EQ(EventToastsFor(after, EventStateFor(Claude(), {next})).upload,
            u"Claude Code attached resume.pdf");
  // A tab of another window: no toast here.
  IndicatorTab elsewhere = Tab("t", true, TabState::kDriven, false, false);
  elsewhere.attached = u"resume.pdf";
  elsewhere.attached_seq = 6;
  EXPECT_EQ(EventToastsFor(before, EventStateFor(Claude(), {elsewhere})).upload,
            u"");
}

}  // namespace
}  // namespace koan::agent
