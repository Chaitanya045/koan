// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_AGENT_INDICATOR_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_AGENT_INDICATOR_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "koan/browser/agent/koan_agent_indicator_logic.h"
#include "koan/browser/agent/koan_agent_service.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/view_tracker.h"

class BrowserView;

namespace content {
class WebContents;
}

namespace views {
class MenuRunner;
class View;
class Widget;
}  // namespace views

namespace koan {

// "An agent is connected" for one window (.agent/notes/agent-control.md
// section 6.2, threat T10), in layers (koan_agent_indicator_logic.h decides
// which shows):
// - the pill, on the tab an agent controls while it is the one shown: a
//   child widget in the page's bottom corner away from the sidebar, in the
//   toast's colours, "● Claude Code is controlling this tab · Take back ·
//   Stop", and an accent border round that page (a child widget that takes
//   no events); a page waiting for the user there shows Open and Dismiss;
// - the chip, in the sidebar above the Spaces' bottom bar, while any agent
//   is connected: a dot and the agent's name, how many tabs wait for the user
//   elsewhere, and a menu that goes to them or stops every agent. While the
//   sidebar is hidden (compact mode, full screen, no sidebar) and no pill
//   shows, a dot pill in the page's corner stands in for it, with the same
//   menu, so one layer shows in every layout;
// - the agent's tabs in the sidebar carry the same dot
//   (PaintSidebarTab(), IsAgentTab());
// - toasts (KoanToast) when an agent connects or disconnects (in the front
//   window) and when the user's input pauses a tab of this window.
// Stop closes every agent connection and forgets every pairing
// (AgentControlService::StopAllAgents()).
class KoanAgentIndicator : public koan::agent::AgentControlService::Observer,
                           public TabStripModelObserver,
                           public ui::SimpleMenuModel::Delegate {
 public:
  explicit KoanAgentIndicator(BrowserView* browser_view);
  KoanAgentIndicator(const KoanAgentIndicator&) = delete;
  KoanAgentIndicator& operator=(const KoanAgentIndicator&) = delete;
  ~KoanAgentIndicator() override;

  // Called by KoanBrowserViewController.
  void OnAddedToWidget();
  // Keeps the pill, the border and the corner dot in place, and puts the chip
  // back in a rebuilt sidebar.
  void OnBrowserViewLayout();
  void OnBrowserViewDestroying();

  // Whether an agent holds `contents`, a tab of this window: the sidebar
  // marks it.
  bool IsAgentTab(const content::WebContents* contents) const;

  // Opens the chip's menu at `anchor` (a view of this window).
  void ShowMenu(views::View* anchor);

  bool IsShowingForTesting() const;

 private:
  // What agent control reports, for this window.
  struct Snapshot {
    std::vector<koan::agent::IndicatorClient> clients;
    std::vector<koan::agent::IndicatorTab> tabs;
    std::vector<koan::agent::IndicatorPrompt> prompts;
    std::set<const content::WebContents*> tabs_in_window;
    // The tab of each target and of each prompt.
    std::map<std::string, content::WebContents*> tab_contents;
    std::map<uint64_t, content::WebContents*> prompt_contents;
  };
  Snapshot TakeSnapshot() const;

  // koan::agent::AgentControlService::Observer:
  void OnAgentControlChanged() override;

  // TabStripModelObserver: the shown tab changed.
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnSplitTabChanged(const SplitTabChange& change) override;

  // ui::SimpleMenuModel::Delegate (the chip's menu):
  void ExecuteCommand(int command_id, int event_flags) override;

  void Update();
  void ShowEventToasts(const koan::agent::EventState& state);
  void ShowPill(const koan::agent::PillContent& content);
  void HidePill();
  void UpdateBorder();
  void InstallChip();
  void UpdateChip();
  // The corner dot, while the sidebar's chip can't be seen.
  void UpdateCornerDot();
  // Runs UpdateCornerDot() and installs the chip after the layout under way.
  void ScheduleLayoutUpdate();
  // Repaints the sidebar's tabs whose mark changed.
  void RepaintTabs();
  // Puts `widget` in the page's bottom corner away from the sidebar, at its
  // contents' preferred size.
  void PlaceInCorner(views::Widget* widget);

  const raw_ptr<BrowserView> browser_view_;
  bool torn_down_ = false;
  koan::agent::IndicatorContent content_;
  koan::agent::EventState events_;
  std::set<const content::WebContents*> marked_;
  raw_ptr<content::WebContents> pill_contents_ = nullptr;
  bool layout_update_pending_ = false;

  std::unique_ptr<views::Widget> pill_;
  std::unique_ptr<views::Widget> border_;
  std::unique_ptr<views::Widget> corner_;
  views::ViewTracker chip_;

  // The chip's menu, and what its items go to.
  std::vector<koan::agent::MenuEntry> menu_entries_;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;

  base::ScopedObservation<koan::agent::AgentControlService,
                          koan::agent::AgentControlService::Observer>
      observation_{this};
  base::WeakPtrFactory<KoanAgentIndicator> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_AGENT_INDICATOR_H_
