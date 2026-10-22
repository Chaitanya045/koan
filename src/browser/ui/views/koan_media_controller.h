// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_MEDIA_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_MEDIA_CONTROLLER_H_

#include <map>
#include <memory>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_tracker.h"

class BrowserView;

namespace content {
class WebContents;
}

namespace koan {

class KoanMediaCard;
class KoanMediaControlsView;

// Zen's media controller (ZenMediaController.mjs) for one window: a card at
// the bottom of the sidebar for media playing in another tab, with the
// site's icon, previous / play-pause / next and mute; hovering it shows the
// title, artist, progress (seekable), picture-in-picture and close. Up to
// three cards stack, newest in front, the others peeking out above it.
//
// Like Zen, a card appears once a tab has played sound for a second through
// a media session (so notification pings don't show one), hides while its
// tab is the selected one or in picture-in-picture, and comes back half a
// second after switching away. Clicking a card selects its tab (and its
// workspace). The card goes when the media session ends or the tab closes;
// close pauses the media and dismisses the card until it plays again.
class KoanMediaController : public TabStripModelObserver,
                            public views::ViewObserver {
 public:
  explicit KoanMediaController(BrowserView* browser_view);
  KoanMediaController(const KoanMediaController&) = delete;
  KoanMediaController& operator=(const KoanMediaController&) = delete;
  ~KoanMediaController() override;

  // Called by KoanBrowserViewController.
  void OnAddedToWidget();
  void OnBrowserViewLayout();
  void OnBrowserViewDestroying();

  // Called by the cards.
  void OnCardChanged(KoanMediaCard* card);
  void RemoveCardSoon(KoanMediaCard* card);
  // Selects the tab of `contents`, switching workspace if needed.
  void FocusTab(content::WebContents* contents);

  // The cards to show, newest first.
  std::vector<KoanMediaCard*> GetShownCards() const;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // views::ViewObserver (the sidebar):
  void OnViewBoundsChanged(views::View* observed_view) override;
  void OnViewIsDeleting(views::View* observed_view) override;

 private:
  class TabWatcher;

  bool IsEnabled() const;
  void Watch(content::WebContents* contents);
  void OnAudibleChanged(content::WebContents* contents, bool audible);
  // A second after the sound started: show the card if still playing.
  void CheckStillPlaying(content::WebContents* contents);
  void OnActiveTabChanged();
  void RevealAfterTabSwitch();
  KoanMediaCard* CardFor(content::WebContents* contents) const;
  void RemoveCard(KoanMediaCard* card);
  void SetCardShown(KoanMediaCard* card, bool shown);

  void InstallView();
  KoanMediaControlsView* view();
  void UpdateView();
  void UpdateVisibility();

  const raw_ptr<BrowserView> browser_view_;
  std::map<content::WebContents*, std::unique_ptr<TabWatcher>> watchers_;
  // Oldest first.
  std::vector<std::unique_ptr<KoanMediaCard>> cards_;
  views::ViewTracker view_;
  base::ScopedObservation<views::View, views::ViewObserver>
      sidebar_observation_{this};
  base::OneShotTimer tab_switch_timer_;

  base::WeakPtrFactory<KoanMediaController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_MEDIA_CONTROLLER_H_
