// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_AGENT_INDICATOR_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_AGENT_INDICATOR_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "koan/browser/agent/koan_agent_service.h"

class BrowserView;

namespace views {
class Widget;
}

namespace koan {

// "An agent is connected" for one window (.agent/notes/agent-control.md
// section 6.2, threat T10): a pill in the window's bottom corner away from the
// sidebar, in the toast's colours, shown for as long as any client is
// connected: "● Claude Code is connected · Stop". It is a child widget above
// the page, so it shows in every layout: compact mode with the sidebar hidden,
// full screen, and every window. Stop closes every agent connection and
// forgets every pairing (AgentControlService::StopAllAgents()).
//
// This is the first layer of the indicator; the agent Space glow, the banner
// on a driven tab and the menu-bar item come with the features they belong to.
class KoanAgentIndicator : public koan::agent::AgentControlService::Observer {
 public:
  explicit KoanAgentIndicator(BrowserView* browser_view);
  KoanAgentIndicator(const KoanAgentIndicator&) = delete;
  KoanAgentIndicator& operator=(const KoanAgentIndicator&) = delete;
  ~KoanAgentIndicator() override;

  // Keeps the pill in its corner.
  void OnBrowserViewLayout();

  // What the pill shows: its text, and the Hand back / Take back buttons for
  // one tab of this window (design section 9.2), or Open / Dismiss for what a
  // page in an agent tab waits to show (section 9.4).
  struct Content {
    std::u16string text;
    std::string target_id;
    bool hand_back = false;
    bool take_back = false;
    uint64_t prompt_id = 0;
    bool operator==(const Content&) const = default;
  };

  // What the pill says, or empty when nothing is connected. Public for tests.
  static std::u16string TextFor(
      const std::vector<koan::agent::ConnectedClient>& clients);
  // The pill for those clients, this window's controlled tabs and every
  // held prompt (an agent tab may be in a window of its own, so each window
  // shows them).
  static Content ContentFor(
      const std::vector<koan::agent::ConnectedClient>& clients,
      const std::vector<koan::agent::ControlledTab>& tabs_in_window,
      const std::vector<koan::agent::HeldPrompt>& prompts);

  bool IsShowingForTesting() const;

 private:
  // koan::agent::AgentControlService::Observer:
  void OnAgentControlChanged() override;

  void Show(const Content& content);
  void Hide();

  const raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<views::Widget> widget_;
  Content shown_;
  base::ScopedObservation<koan::agent::AgentControlService,
                          koan::agent::AgentControlService::Observer>
      observation_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_AGENT_INDICATOR_H_
