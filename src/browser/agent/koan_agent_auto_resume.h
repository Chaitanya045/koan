// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_AUTO_RESUME_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_AUTO_RESUME_H_

#include <optional>
#include <string_view>

#include "base/time/time.h"
#include "koan/browser/agent/koan_agent_limits.h"

namespace base {
class Value;
}

// When a tab the user's input paused goes back to its agent by itself
// (.agent/notes/agent-control.md section 9.2, "Auto-resume"; user decision
// 2026-11-20): the agent uses the browser as the user does, so once the user
// stops using the tab it carries on from where they left it.
//
// Only a pause caused by the user's input in the page resumes by itself. It
// waits for Hand back ("held") once anything says the user is handling the
// tab: the pill's Take over, focus in a password or payment field, a page
// asking for the user (Koan.userNeeded), an interstitial, a dialog or a
// permission prompt, or the pill's Keep paused. A hold lasts until Hand back.
//
// - The user left the tab (it is not shown, its window is not the active one,
//   or Koan is not the active app): `delay` after their last activity in it,
//   the tab resumes ("user-idle").
// - The user stays on the tab: `delay` after their last activity the pill
//   counts down for kAutoResumeCountdown ("continues in 5s"), then the tab
//   resumes.
// Activity is the user's input in the page (which paused it), moving the
// mouse over the page, and coming back to the tab; each restarts the wait and
// ends a countdown. Mouse movement never pauses a tab by itself.
//
// Pure logic over a clock the caller passes in; the browser side
// (koan_agent_forwarder.cc) feeds it what happens and asks Advance() when
// NextCheck() says.
namespace koan::agent {

// Settings > Agent Control: "When you stop using a tab, the agent continues
// after [5] s" (koan_prefs.h kAgentControlAutoResume*), 2 to 60 seconds, or
// "Wait for Hand back". There is no 0 and no "never" value.
inline constexpr LimitSpec kAutoResumeSecondsLimit = {5, 2, 60};
// How long the pill counts down while the user is still on the tab.
inline constexpr base::TimeDelta kAutoResumeCountdown = base::Seconds(5);

struct AutoResumeSettings {
  bool enabled = true;
  base::TimeDelta delay = base::Seconds(kAutoResumeSecondsLimit.default_value);
};

// The settings from their stored prefs (either may be missing or bad): on,
// and the default delay, unless a bool and an in-range integer say otherwise
// (ReadSwitch(), ReadLimit(): 0, negative or out-of-range seconds are never
// used).
AutoResumeSettings ReadAutoResumeSettings(const base::Value* enabled,
                                          const base::Value* seconds);

// Why a pause waits for Hand back.
enum class PauseHold {
  kNone,
  // The pill's Take over (Koan.paused {reason: "user"}).
  kTakeOver,
  // Focus in a password, credit-card or payment field.
  kSecretField,
  // A page in the tab asked for the user (Koan.userNeeded).
  kUserNeeded,
  kInterstitial,
  // A JavaScript dialog or a permission prompt is open.
  kDialog,
  // The pill's Keep paused.
  kKeepPaused,
  // Settings > Agent Control > "Wait for Hand back" (not latched: turning the
  // setting back on lets the pause resume again).
  kSetting,
};

// The name the agent sees (Koan.paused / Koan.listTabs "sticky"); empty for
// kNone.
std::string_view PauseHoldName(PauseHold hold);

class AutoResumePolicy {
 public:
  enum class Phase {
    kNotPaused,
    // Waits for Hand back (hold()).
    kHeld,
    // Waits for the user to stop using the tab.
    kWaiting,
    // The user is on the tab and idle: the pill counts down.
    kCountdown,
  };

  explicit AutoResumePolicy(AutoResumeSettings settings = {});
  AutoResumePolicy(const AutoResumePolicy&) = delete;
  AutoResumePolicy& operator=(const AutoResumePolicy&) = delete;
  ~AutoResumePolicy();

  // The tab was paused, with Koan.paused's `reason`: "user-input" may resume
  // by itself, anything else is held (kTakeOver).
  void Pause(std::string_view reason, bool user_on_tab, base::TimeTicks now);
  // The tab waits for Hand back from now on, for `hold` (the first one is
  // kept). No-op for kNone, kSetting or when not paused.
  void Hold(PauseHold hold);
  // The user's input in the page (a click, a key, a scroll, a touch).
  void OnInput(base::TimeTicks now);
  // The mouse moved over the page: the user is still there.
  void OnMouseMove(base::TimeTicks now);
  // Whether the user is on the tab now (shown, in the active window of the
  // active app). Coming back counts as activity.
  void SetUserOnTab(bool user_on_tab, base::TimeTicks now);
  void SetSettings(AutoResumeSettings settings);

  // Moves the countdown on; true once the tab is due to resume. The caller
  // resumes it and calls End() (or OnInput() if the user came back first).
  bool Advance(base::TimeTicks now);
  // Hand back, Stop, revocation, the tab closing: no pause any more.
  void End();

  Phase phase() const;
  bool paused() const { return paused_; }
  // Why it waits for Hand back: the latched hold, or kSetting while the
  // setting is "Wait for Hand back"; kNone if it may resume by itself.
  PauseHold hold() const;
  bool user_on_tab() const { return user_on_tab_; }
  base::TimeTicks paused_since() const { return paused_since_; }
  // When Advance() must run next for the pause to move on (nullopt: never,
  // until something happens).
  std::optional<base::TimeTicks> NextCheck() const;
  // When the tab will resume if nothing else happens, nullopt if held.
  std::optional<base::TimeTicks> ResumesAt() const;
  // The end of the pill's countdown, while it counts down.
  std::optional<base::TimeTicks> CountdownEnds() const;

 private:
  bool MayResume() const;

  AutoResumeSettings settings_;
  bool paused_ = false;
  bool user_on_tab_ = false;
  PauseHold hold_ = PauseHold::kNone;
  base::TimeTicks paused_since_;
  // The user's last activity in the tab.
  base::TimeTicks active_at_;
  // Set while the pill counts down.
  std::optional<base::TimeTicks> countdown_ends_;
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_AUTO_RESUME_H_
