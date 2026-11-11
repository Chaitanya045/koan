// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_CLIENT_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_CLIENT_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/tab_groups/tab_group_id.h"
#include "koan/browser/ui/views/koan_window_sync.h"
#include "ui/views/widget/widget_observer.h"

class BrowserView;
class BrowserWindowInterface;
class Profile;

namespace tabs {
class TabInterface;
}

namespace koan {

// One window's side of Zen's window sync (KoanWindowSync): registers a
// synced window with its profile's sync, reports what the user changes in it
// and reconciles it to the synced state. Owned by KoanBrowserViewController,
// for every window; it does nothing in a window that isn't synced (see
// KoanWindowSync::ShouldSync()).
class KoanWindowSyncClient : public KoanWindowSync::Window,
                             public TabStripModelObserver,
                             public views::WidgetObserver {
 public:
  explicit KoanWindowSyncClient(BrowserView* browser_view);
  KoanWindowSyncClient(const KoanWindowSyncClient&) = delete;
  KoanWindowSyncClient& operator=(const KoanWindowSyncClient&) = delete;
  ~KoanWindowSyncClient() override;

  // The client of `browser_view`'s window if it is synced, else null.
  static KoanWindowSyncClient* From(const BrowserView* browser_view);
  static KoanWindowSyncClient* ForBrowser(BrowserWindowInterface* browser);

  // After the window's controllers are set up (its Spaces in particular).
  void OnAddedToWidget();
  // Before they go: the window leaves the sync.
  void OnBrowserViewDestroying();

  // Whether the window takes part in the sync.
  bool is_synced() const { return sync_ != nullptr; }
  KoanWindowSync* sync() const { return sync_; }
  // Whether the sync is changing this window right now (KoanWindowSync::
  // IsApplying()): its controllers leave what changes alone.
  bool IsApplying() const;

  // KoanWindowSync::Window:
  KoanWindowSync::WindowId window_id() const override;
  void ApplySpaces(
      const std::vector<window_sync::SpaceRecord>& spaces) override;
  std::vector<window_sync::SpaceRecord> AdoptSavedSpaces(
      const std::vector<window_sync::SpaceRecord>& saved) override;
  void ApplyItems(const std::vector<window_sync::ItemRecord>& items) override;
  bool CanGivePage(const std::string& item) const override;
  bool CanTakePage(const std::string& item) const override;
  std::unique_ptr<content::WebContents> TakePage(
      const std::string& item) override;
  void PutPage(const std::string& item,
               std::unique_ptr<content::WebContents> page) override;
  void LoadPage(const window_sync::ItemRecord& item) override;
  content::WebContents* PageOf(const std::string& item) const override;
  void RefreshPlaceholder(const std::string& item) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      TabChangeType change_type) override;
  void OnTabPinnedStateChanged(tabs::TabInterface* tab, int index) override;
  void TabGroupedStateChanged(TabStripModel* tab_strip_model,
                              std::optional<tab_groups::TabGroupId> old_group,
                              std::optional<tab_groups::TabGroupId> new_group,
                              tabs::TabInterface* tab,
                              int index) override;

  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  // Joins the sync, once the session restore (if any) gave the window its
  // Spaces.
  void Join();
  void OnSessionRestored(Profile* profile, int tab_count);
  void JoinAfterRestore();

  // --- Spaces ---

  // The window's Spaces that sync, in order: not an agent container's Space
  // (koan/browser/containers), which stays in its window. With
  // `assign_ids`, a Space without a sync id gets a new one; otherwise those
  // are left out (and listed in `unmapped`, if given).
  std::vector<window_sync::SpaceRecord> LocalSpaces(
      bool assign_ids,
      std::vector<tab_groups::TabGroupId>* unmapped = nullptr);
  // The Space with sync id `id` in this window, if any.
  std::optional<tab_groups::TabGroupId> FindSpace(const std::string& id) const;
  void OnLocalSpacesChanged();
  // The window's map of tab groups to sync ids, kept with the window in the
  // session. Only for its groups: a session restore gives the groups new ids
  // (session_restore.cc relabels them), so a restored window's map names
  // none of them, and its Spaces take their ids back by name (ApplySpaces(),
  // AdoptSavedSpaces()).
  void LoadSpaceIds();
  void SaveSpaceIds();

  // --- Folders ---

  // The window's synced folders, in sidebar order: those of its synced
  // Spaces. With `assign_ids`, a folder that isn't synced yet becomes one
  // (its token is its sync id); otherwise those are left out.
  std::vector<window_sync::FolderRecord> LocalFolders(bool assign_ids);
  // Reconciles the window's folders to `target` (before its items).
  void ApplyFolders(const std::vector<window_sync::FolderRecord>& target);
  // Puts each tab of `target` in its folder.
  void ApplyFolderMembership(
      const std::vector<window_sync::ItemRecord>& target);
  // The tokens of the window's synced folders, kept with the window in the
  // session (a joining window's own folders aren't, until it reports them).
  void LoadFolderIds();
  void SaveFolderIds();

  // --- Essentials and pinned tabs ---

  // The window's synced tabs, in sidebar order: its Essentials and the
  // pinned tabs of its synced Spaces, not an agent container's tabs nor a
  // split view's. With `assign_ids`, a tab without a sync id gets one;
  // otherwise those are left out.
  std::vector<window_sync::ItemRecord> LocalItems(bool assign_ids);
  // The tab of synced tab `item` in this window, or null.
  tabs::TabInterface* FindItemTab(const std::string& item) const;
  // Reports the window's items (and the pages it has) once its tabs settle
  // (after KoanPinnedTabController's Normalize()).
  void ScheduleItemsReport();
  void ReportItems();
  // The active tab is a placeholder: its page comes here.
  void MaybeRequestActivePage();
  // Puts the tabs of `target` in its order: the Essentials first, then each
  // Space's pinned tabs at the top of the Space.
  void OrderItems(const std::vector<window_sync::ItemRecord>& target);

  const raw_ptr<BrowserView> browser_view_;
  // Set while the window is synced.
  raw_ptr<KoanWindowSync> sync_ = nullptr;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
  base::CallbackListSubscription session_restored_subscription_;
  base::CallbackListSubscription spaces_changed_subscription_;
  std::map<tab_groups::TabGroupId, std::string> space_ids_;
  base::CallbackListSubscription folders_changed_subscription_;
  std::set<std::string> synced_folders_;
  // The synced tabs the user closed here since the last report.
  std::set<std::string> closed_items_;
  base::OneShotTimer items_report_timer_;
  base::WeakPtrFactory<KoanWindowSyncClient> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_WINDOW_SYNC_CLIENT_H_
