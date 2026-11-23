// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_INDICATOR_LOGIC_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_INDICATOR_LOGIC_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "koan/browser/agent/koan_agent_prompts.h"
#include "koan/browser/agent/koan_agent_registry.h"

// What the agent indicator shows in one window (.agent/notes/agent-control.md
// section 6.2, threat T10), as pure logic over what agent control reports.
// The indicator is layered:
// - the pill, on the tab an agent controls, while that tab is the one shown:
//   "<agent> is controlling this tab · Take back · Stop", with an accent
//   border round the page; a page in that tab waiting for the user
//   (section 9.4) takes the pill's place with Open and Dismiss;
// - the chip, in every other case while any agent is connected: the agent's
//   name and a dot in the sidebar (or a dot pill in the window's corner while
//   the sidebar is hidden), with how many tabs wait for the user elsewhere,
//   and a menu that goes to the agents' tabs or stops every agent.
// koan_agent_indicator.cc (//koan/browser/ui/views) draws them.
namespace koan::agent {

// A connected client (one per connection: a client may hold several).
struct IndicatorClient {
  std::string client_id;
  std::string label;
};

// A tab an agent holds: its own, or a user tab it was given.
struct IndicatorTab {
  std::string target_id;
  // The client's label.
  std::string label;
  // The tab's title, for the chip's menu.
  std::u16string title;
  TabState state = TabState::kDriven;
  bool agent_owned = false;
  // The agent asked to resume (Koan.resume).
  bool resume_asked = false;
  // Paused, and it goes back to the agent by itself once the user stops
  // using the tab (design section 9.2, auto-resume).
  bool auto_resumes = false;
  // While it counts down to that: the seconds left (at least 1).
  std::optional<int> countdown_seconds;
  // For a few seconds after the agent attached files to a page in the tab
  // (`koan upload`): their names, and which upload it was.
  std::u16string attached;
  uint64_t attached_seq = 0;
  // In this window's tab strip, and shown there (the active tab, or a pane
  // of the active split).
  bool in_window = false;
  bool shown = false;
};

// A page in an agent's tab waiting for the user (section 9.4).
struct IndicatorPrompt {
  UserPrompt prompt;
  // Its tab is shown in this window.
  bool shown = false;
};

// The pill on the shown tab, and what its buttons act on.
struct PillContent {
  std::u16string text;
  std::string target_id;
  // Hand a paused tab back to its agent.
  bool hand_back = false;
  // Take a user tab back from its agent.
  bool take_back = false;
  // Pause an agent's own tab (as the user's input in it would).
  bool take_over = false;
  // Make a pause that would resume by itself wait for Hand back.
  bool keep_paused = false;
  // The countdown to the agent going on ("continues in 5s"), in `text`; only
  // the seconds change from one second to the next.
  std::optional<int> countdown_seconds;
  // Open and Dismiss a page's prompt.
  uint64_t prompt_id = 0;
  bool operator==(const PillContent&) const = default;
};

// The chip: who is connected, and how many tabs wait for the user away from
// the shown one (paused tabs and pages' prompts).
struct ChipContent {
  // "Claude Code", "2 agents", with " · 1 waiting" when some do (the view
  // draws the dot).
  std::u16string text;
  // For screen readers and the tooltip: "Claude Code is connected", and
  // what waits.
  std::u16string accessible_name;
  size_t waiting = 0;
  bool operator==(const ChipContent&) const = default;
};

struct IndicatorContent {
  // Nothing shows while no agent is connected.
  bool connected = false;
  // The shown tab is an agent's (or a page in it waits): the pill, and the
  // accent border round the page.
  std::optional<PillContent> pill;
  // The window's chip, while connected.
  ChipContent chip;
  bool operator==(const IndicatorContent&) const = default;
};

// The indicator for one window: `tabs` are every tab an agent holds (with
// `in_window` and `shown` for this window), `prompts` every held prompt.
// `keeping_display_awake`: Koan holds the display-sleep assertion for an agent
// at work (design section 8); the chip's tooltip says so.
IndicatorContent IndicatorFor(const std::vector<IndicatorClient>& clients,
                              const std::vector<IndicatorTab>& tabs,
                              const std::vector<IndicatorPrompt>& prompts,
                              bool keeping_display_awake = false);

// One item of the chip's menu: go to a tab an agent holds, or to the tab of
// a page's prompt. "Stop all agents" follows them.
struct MenuEntry {
  std::u16string label;
  std::string target_id;
  uint64_t prompt_id = 0;
  bool operator==(const MenuEntry&) const = default;
};

// The menu's items, for what is not shown in this window ("Go to <title>
// (<agent>)" for a tab, the prompt's own text for a prompt): prompts
// first (oldest first), then paused tabs, then the others; at most one item
// per tab.
std::vector<MenuEntry> MenuFor(const std::vector<IndicatorTab>& tabs,
                               const std::vector<IndicatorPrompt>& prompts);

// What the event toasts compare (design section 6.2, "Events"): the clients
// connected, the tabs paused, by id, with their agent's label, and the
// uploads to this window's tabs ("<label>\n<seq>" -> what was attached).
struct EventState {
  std::map<std::string, std::string> clients;
  std::map<std::string, std::string> paused;
  std::map<std::string, std::u16string> attached;
  bool operator==(const EventState&) const = default;
};
EventState EventStateFor(const std::vector<IndicatorClient>& clients,
                         const std::vector<IndicatorTab>& tabs);

// The toasts for a change from `before` to `after`: an agent connected or
// disconnected (for the front window), a tab of this window paused, an agent
// attached files to a page in a tab of this window ("Claude Code attached
// resume.pdf"). Empty when nothing of that kind changed. With several
// changes at once, the toast names the first (by id); a toast replaces the
// one before anyway.
struct EventToasts {
  std::u16string connection;
  std::u16string pause;
  std::u16string upload;
};
EventToasts EventToastsFor(const EventState& before, const EventState& after);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_INDICATOR_LOGIC_H_
