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

  // What the pill says, or empty when nothing is connected. Public for tests.
  static std::u16string TextFor(
      const std::vector<koan::agent::ConnectedClient>& clients);

  bool IsShowingForTesting() const;

 private:
  // koan::agent::AgentControlService::Observer:
  void OnAgentControlChanged() override;

  void Show(const std::u16string& text);
  void Hide();

  const raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<views::Widget> widget_;
  base::ScopedObservation<koan::agent::AgentControlService,
                          koan::agent::AgentControlService::Observer>
      observation_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_AGENT_INDICATOR_H_
