// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// KoanWindowSyncClient's Essentials and pinned tabs (see
// koan_window_sync_client.h and .agent/notes/upstream/window-sync.md): every
// synced window has every synced tab; one has its page, the others a
// placeholder, and the page moves to the window that shows the tab.

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_removed_reason.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_data.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/invalidate_type.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_folder_controller.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_session_data.h"
#include "koan/browser/ui/views/koan_window_sync_client.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/range/range.h"
#include "url/gurl.h"

namespace koan {

namespace {

// A synced tab's id, in its tab's session data (koan_session_data.h): it
// follows the page between windows and comes back with the session.
constexpr char kItemIdSessionKey[] = "koan.window_sync.item";

// After KoanPinnedTabController's Normalize() (400 ms), which settles a
// drag's or a pin's tabs: what the window reports is their final state.
constexpr base::TimeDelta kItemsReportDelay = base::Milliseconds(500);

// Marks a placeholder: a synced tab's contents in a window that doesn't
// have its page. It never loads (no renderer, no reload), and shows the
// page's title and favicon (its history is a copy of the page's).
class Placeholder : public content::WebContentsUserData<Placeholder> {
 public:
  ~Placeholder() override = default;

 private:
  friend WebContentsUserData;
  explicit Placeholder(content::WebContents* contents)
      : WebContentsUserData(*contents) {}
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(Placeholder);

bool IsPlaceholder(content::WebContents* contents) {
  return contents && Placeholder::FromWebContents(contents);
}

std::string ItemIdOf(content::WebContents* contents) {
  return contents ? GetTabSessionData(contents, kItemIdSessionKey)
                  : std::string();
}

// Writes `contents`' Koan session data again, under the tab and window it is
// in now (a replace doesn't carry it to the session, unlike a discard's
// AboutToBeDiscarded()). Soon: the session learns of the tab in the replace's
// notifications.
void RewriteSessionDataSoon(content::WebContents* contents) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<content::WebContents> contents) {
            if (!contents) {
              return;
            }
            for (auto& [key, data] : GetTabSessionData(contents.get())) {
              SetTabSessionData(contents.get(), key, data);
            }
          },
          contents->GetWeakPtr()));
}

// A placeholder for synced tab `record`: in the container `page` is in (or
// the record's, with no page), with a copy of `page`'s history so its row
// shows the page's title and favicon, and its Koan data.
std::unique_ptr<content::WebContents> NewPlaceholder(
    Profile* profile,
    content::WebContents* page,
    const window_sync::ItemRecord& record) {
  content::WebContents::CreateParams params(profile);
  params.initially_hidden = true;
  params.desired_renderer_state =
      content::WebContents::CreateParams::kNoRendererProcess;
  // Never the default partition for a container's page.
  if (page) {
    params.site_instance = containers::SiteInstanceForReplacement(page);
  } else if (std::optional<containers::ContainerId> container =
                 containers::Parse(record.container);
             container && container->kind == containers::ContainerKind::kUser) {
    params.site_instance =
        containers::CreateSiteInstance(profile, *container, GURL(record.url));
  }
  std::unique_ptr<content::WebContents> contents =
      content::WebContents::Create(params);
  if (page) {
    // As a discard does: no reload when it is shown.
    contents->GetController().CopyStateFrom(&page->GetController(),
                                            /*needs_reload=*/false);
    for (const auto& [key, data] : GetTabSessionData(page)) {
      SetTabSessionData(contents.get(), key, data);
    }
  }
  SetTabSessionData(contents.get(), kItemIdSessionKey, record.id);
  Placeholder::CreateForWebContents(contents.get());
  return contents;
}

// Closes `tab`, a synced tab closed in another window: without Recently
// Closed (that window has the entry). A Space (a tab group) goes away with
// its last tab, so one that would be left empty gets a new tab first, as
// Zen's Spaces keep one.
void CloseSyncedTab(BrowserWindowInterface* browser, tabs::TabInterface* tab) {
  TabStripModel* model = browser->GetTabStripModel();
  const int index = model->GetIndexOfTab(tab);
  if (index == TabStripModel::kNoTab) {
    return;
  }
  if (std::optional<tab_groups::TabGroupId> group = tab->GetGroup();
      group && model->group_model() &&
      model->group_model()->ContainsTabGroup(*group) &&
      model->group_model()->GetTabGroup(*group)->tab_count() == 1) {
    chrome::AddTabAt(browser, GURL(), index + 1,
                     /*foreground=*/tab->IsActivated(), group);
  }
  model->CloseWebContentsAt(model->GetIndexOfTab(tab),
                            TabCloseTypes::CLOSE_NONE);
}

}  // namespace

void AdjustPlaceholderTabData(const tabs::TabInterface* tab,
                              tabs::TabData& data) {
  content::WebContents* contents = tab->GetContents();
  if (!IsPlaceholder(contents)) {
    return;
  }
  KoanWindowSync* sync = KoanWindowSync::ForProfile(
      Profile::FromBrowserContext(contents->GetBrowserContext()));
  content::WebContents* page =
      sync ? sync->LivePage(ItemIdOf(contents)) : nullptr;
  tabs::TabInterface* page_tab =
      page && page != contents ? tabs::TabInterface::MaybeGetFromContents(page)
                               : nullptr;
  TabUIHelper* helper = page_tab ? TabUIHelper::From(page_tab) : nullptr;
  if (!helper) {
    return;
  }
  data.title = helper->GetTitle();
  data.favicon = helper->GetFavicon();
  data.should_themify_favicon = helper->ShouldThemifyFavicon();
  data.is_monochrome_favicon = helper->IsMonochromeFavicon();
}

// static
KoanWindowSyncClient* KoanWindowSyncClient::ForBrowser(
    BrowserWindowInterface* browser) {
  return browser ? From(BrowserView::GetBrowserViewForBrowser(browser))
                 : nullptr;
}

std::vector<window_sync::ItemRecord> KoanWindowSyncClient::LocalItems(
    bool assign_ids) {
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  KoanFolderController* folders =
      KoanBrowserViewController::From(browser_view_)->folders();
  if (folders && !folders->IsEnabled()) {
    folders = nullptr;
  }
  std::vector<window_sync::ItemRecord> essentials;
  std::map<std::string, std::vector<window_sync::ItemRecord>> pinned;
  std::vector<std::string> space_order;
  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    content::WebContents* contents = tab->GetContents();
    const bool essential = tab->IsPinned();
    if ((!essential && !IsPinnedInSpace(tab)) || tab->IsSplit() || !contents ||
        containers::IsAgentTab(contents)) {
      continue;
    }
    std::string space_id;
    if (!essential) {
      std::optional<tab_groups::TabGroupId> group = model->GetTabGroupForTab(i);
      auto it = group ? space_ids_.find(*group) : space_ids_.end();
      if (it == space_ids_.end()) {
        continue;
      }
      space_id = it->second;
    }
    std::string id = ItemIdOf(contents);
    if (id.empty()) {
      if (!assign_ids) {
        continue;
      }
      id = base::Uuid::GenerateRandomV4().AsLowercaseString();
      SetTabSessionData(contents, kItemIdSessionKey, id);
    }
    const KoanTabState* state = GetTabState(tab);
    std::optional<containers::ContainerId> container =
        containers::ContainerOf(contents);
    window_sync::ItemRecord record{
        .id = std::move(id),
        .kind = essential ? window_sync::ItemRecord::Kind::kEssential
                          : window_sync::ItemRecord::Kind::kPinned,
        .space_id = space_id,
        .url = state ? state->pinned_url.spec() : std::string(),
        .custom_title = state ? state->custom_title : std::u16string(),
        .custom_icon = state ? state->custom_icon : std::u16string(),
        .container =
            container ? containers::Serialize(*container) : std::string()};
    if (const KoanFolder* folder =
            !essential && folders
                ? folders->GetFolder(folders->GetFolderOf(tab))
                : nullptr;
        folder && synced_folders_.contains(folder->token)) {
      record.folder_id = folder->token;
    }
    if (essential) {
      essentials.push_back(std::move(record));
    } else {
      if (!pinned.contains(space_id)) {
        space_order.push_back(space_id);
      }
      pinned[space_id].push_back(std::move(record));
    }
  }
  std::vector<window_sync::ItemRecord> items = std::move(essentials);
  for (const std::string& space : space_order) {
    for (window_sync::ItemRecord& record : pinned[space]) {
      items.push_back(std::move(record));
    }
  }
  return items;
}

tabs::TabInterface* KoanWindowSyncClient::FindItemTab(
    const std::string& item) const {
  if (item.empty()) {
    return nullptr;
  }
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  for (int i = 0; i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (ItemIdOf(tab->GetContents()) == item) {
      return tab;
    }
  }
  return nullptr;
}

void KoanWindowSyncClient::ScheduleItemsReport() {
  if (!sync_ || IsApplying()) {
    return;
  }
  items_report_timer_.Start(FROM_HERE, kItemsReportDelay, this,
                            &KoanWindowSyncClient::ReportItems);
}

void KoanWindowSyncClient::ReportItems() {
  // What was closed counts only with the report it came before.
  std::set<std::string> closed = std::exchange(closed_items_, {});
  if (!sync_ || !sync_->IsInSync(this) ||
      browser_view_->browser()->GetTabStripModel()->closing_all()) {
    return;
  }
  // Its folders first: the items name them.
  sync_->ReportFolders(this, LocalFolders(/*assign_ids=*/true));
  std::vector<window_sync::ItemRecord> items = LocalItems(/*assign_ids=*/true);
  // The pages this window has, so the others' placeholders copy them.
  for (const window_sync::ItemRecord& record : items) {
    if (!IsPlaceholder(PageOf(record.id))) {
      sync_->SetLive(this, record.id);
    }
  }
  // A tab that stopped being synced here (unpinned, out of the Essentials,
  // into a split) loses its id; a placeholder of one loads its page. Unless
  // it went into a split, the other windows close theirs (Zen's
  // TabUnpinned).
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  std::set<std::string> ids;
  for (const window_sync::ItemRecord& record : items) {
    ids.insert(record.id);
  }
  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    const std::string id = ItemIdOf(contents);
    if (id.empty() || ids.contains(id)) {
      continue;
    }
    if (!model->GetTabAtIndex(i)->IsSplit()) {
      closed.insert(id);
    }
    SetTabSessionData(contents, kItemIdSessionKey, std::string());
    if (IsPlaceholder(contents)) {
      contents->RemoveUserData(Placeholder::UserDataKey());
      contents->GetController().LoadIfNecessary();
    }
  }
  sync_->ReportItems(this, std::move(items), closed);
}

void KoanWindowSyncClient::MaybeRequestActivePage() {
  if (!sync_ || !widget_observation_.IsObserving() ||
      !widget_observation_.GetSource()->IsActive()) {
    return;
  }
  tabs::TabInterface* tab =
      browser_view_->browser()->GetTabStripModel()->GetActiveTab();
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (IsPlaceholder(contents)) {
    sync_->RequestShow(this, ItemIdOf(contents));
  }
}

// --- KoanWindowSync::Window ---

content::WebContents* KoanWindowSyncClient::PageOf(
    const std::string& item) const {
  tabs::TabInterface* tab = FindItemTab(item);
  return tab ? tab->GetContents() : nullptr;
}

void KoanWindowSyncClient::RefreshPlaceholder(const std::string& item) {
  tabs::TabInterface* tab = FindItemTab(item);
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (IsPlaceholder(contents)) {
    // Browser's UI update for the tab: coalesced and run later, then the
    // row asks AdjustPlaceholderTabData() again.
    contents->NotifyNavigationStateChanged(content::INVALIDATE_TYPE_TAB);
  }
}

bool KoanWindowSyncClient::CanGivePage(const std::string& item) const {
  tabs::TabInterface* tab = FindItemTab(item);
  return tab && !tab->IsSplit() && !IsPlaceholder(tab->GetContents()) &&
         !browser_view_->browser()->GetTabStripModel()->closing_all();
}

bool KoanWindowSyncClient::CanTakePage(const std::string& item) const {
  tabs::TabInterface* tab = FindItemTab(item);
  return tab && !tab->IsSplit() && IsPlaceholder(tab->GetContents()) &&
         !browser_view_->browser()->GetTabStripModel()->closing_all();
}

std::unique_ptr<content::WebContents> KoanWindowSyncClient::TakePage(
    const std::string& item) {
  tabs::TabInterface* tab = FindItemTab(item);
  if (!tab || !CanGivePage(item)) {
    return nullptr;
  }
  content::WebContents* page = tab->GetContents();
  const window_sync::ItemRecord record{.id = item};
  // The source's replace comes first: it takes the page's lifecycle unit
  // (TabLifecycleUnitSource::OnTabReplaced DCHECKs that a replacing
  // contents has none). KoanPinnedTabController carries the tab's Koan state
  // over to the placeholder.
  std::unique_ptr<content::WebContents> placeholder =
      NewPlaceholder(browser_view_->GetProfile(), page, record);
  content::WebContents* raw_placeholder = placeholder.get();
  std::unique_ptr<content::WebContents> taken =
      browser_view_->browser()->GetTabStripModel()->DiscardWebContents(
          page, std::move(placeholder));
  RewriteSessionDataSoon(raw_placeholder);
  return taken;
}

void KoanWindowSyncClient::PutPage(const std::string& item,
                                   std::unique_ptr<content::WebContents> page) {
  tabs::TabInterface* tab = FindItemTab(item);
  if (!tab || !CanTakePage(item) ||
      page->GetBrowserContext() != browser_view_->GetProfile()) {
    // Can't happen (KoanWindowSync checks first): the page goes rather than
    // being leaked.
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                               std::move(page));
    return;
  }
  content::WebContents* raw_page = page.get();
  std::unique_ptr<content::WebContents> placeholder =
      browser_view_->browser()->GetTabStripModel()->DiscardWebContents(
          tab->GetContents(), std::move(page));
  // After the replace's notifications are done with it.
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, std::move(placeholder));
  RewriteSessionDataSoon(raw_page);
}

void KoanWindowSyncClient::LoadPage(const window_sync::ItemRecord& item) {
  tabs::TabInterface* tab = FindItemTab(item.id);
  content::WebContents* contents = tab ? tab->GetContents() : nullptr;
  if (!IsPlaceholder(contents)) {
    return;
  }
  contents->RemoveUserData(Placeholder::UserDataKey());
  // Its copied history, else Zen's pinned URL.
  if (contents->GetController().GetEntryCount() > 0) {
    contents->GetController().Reload(content::ReloadType::NORMAL,
                                     /*check_for_repost=*/false);
  } else if (GURL url(item.url); url.is_valid()) {
    contents->GetController().LoadURL(url, content::Referrer(),
                                      ui::PAGE_TRANSITION_AUTO_BOOKMARK,
                                      std::string());
  }
}

void KoanWindowSyncClient::ApplyItems(
    const std::vector<window_sync::ItemRecord>& target) {
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  if (!controller || model->closing_all()) {
    return;
  }
  KoanPinnedTabController* pinned = controller->pinned_tabs();
  // The folders first: the items go into them.
  ApplyFolders(sync_->model().folders());
  const window_sync::Plan<window_sync::ItemRecord> plan =
      window_sync::PlanItems(LocalItems(/*assign_ids=*/false), target);

  for (const std::string& id : plan.remove) {
    tabs::TabInterface* tab = FindItemTab(id);
    if (!tab) {
      continue;
    }
    content::WebContents* contents = tab->GetContents();
    // Closed elsewhere (Zen's TabClose): it closes here too, page or not.
    if (IsPlaceholder(contents) || sync_->model().WasClosed(id)) {
      CloseSyncedTab(browser_view_->browser(), tab);
      continue;
    }
    // Taken into a split elsewhere, and the page is here: it stays, as a
    // normal tab of the Space it is in (or the active one). (Unpinned or out
    // of the Essentials elsewhere counts as closed, as in Zen.)
    SetTabSessionData(contents, kItemIdSessionKey, std::string());
    if (tab->IsPinned()) {
      pinned->RemoveFromEssentials(tab);
    } else {
      pinned->UnpinFromSpace(tab);
    }
  }

  for (const auto& [record, index] : plan.add) {
    std::optional<tab_groups::TabGroupId> group;
    if (record.kind == window_sync::ItemRecord::Kind::kPinned) {
      for (const auto& [space, id] : space_ids_) {
        if (id == record.space_id) {
          group = space;
        }
      }
      if (!group || !model->group_model()->ContainsTabGroup(*group)) {
        continue;
      }
    }
    std::unique_ptr<content::WebContents> placeholder = NewPlaceholder(
        browser_view_->GetProfile(), sync_->LivePage(record.id), record);
    content::WebContents* raw = placeholder.get();
    const bool essential =
        record.kind == window_sync::ItemRecord::Kind::kEssential;
    const int at =
        essential
            ? model->IndexOfFirstNonPinnedTab()
            : static_cast<int>(
                  model->group_model()->GetTabGroup(*group)->ListTabs().end());
    model->InsertWebContentsAt(
        at, std::move(placeholder),
        essential ? AddTabTypes::ADD_PINNED : AddTabTypes::ADD_NONE, group);
    const int inserted = model->GetIndexOfWebContents(raw);
    if (inserted == TabStripModel::kNoTab) {
      continue;
    }
    tabs::TabInterface* tab = model->GetTabAtIndex(inserted);
    pinned->SetSyncedPin(tab, essential, GURL(record.url), record.custom_title,
                         record.custom_icon);
    RewriteSessionDataSoon(raw);
  }

  for (const window_sync::ItemRecord& record : plan.update) {
    tabs::TabInterface* tab = FindItemTab(record.id);
    if (!tab) {
      continue;
    }
    const bool essential =
        record.kind == window_sync::ItemRecord::Kind::kEssential;
    pinned->SetSyncedPin(tab, essential, GURL(record.url), record.custom_title,
                         record.custom_icon);
    if (!essential) {
      for (const auto& [space, id] : space_ids_) {
        const int index = model->GetIndexOfTab(tab);
        if (id == record.space_id && model->GetTabGroupForTab(index) != space &&
            model->group_model()->ContainsTabGroup(space)) {
          model->AddToExistingGroup({index}, space, /*add_to_end=*/false);
        }
      }
    }
  }

  ApplyFolderMembership(target);
  OrderItems(target);
}

void KoanWindowSyncClient::ApplyFolderMembership(
    const std::vector<window_sync::ItemRecord>& target) {
  KoanFolderController* folders =
      KoanBrowserViewController::From(browser_view_)->folders();
  if (!folders || !folders->IsEnabled()) {
    return;
  }
  for (const window_sync::ItemRecord& record : target) {
    tabs::TabInterface* tab = FindItemTab(record.id);
    if (!tab || record.kind != window_sync::ItemRecord::Kind::kPinned ||
        !IsPinnedInSpace(tab)) {
      continue;
    }
    // A folder of the window's own (not synced yet) keeps its tabs.
    const KoanFolder* current = folders->GetFolder(folders->GetFolderOf(tab));
    if (current && !synced_folders_.contains(current->token)) {
      continue;
    }
    const int folder = folders->FindFolderByToken(record.folder_id);
    if (!record.folder_id.empty() && !folder) {
      continue;
    }
    folders->SetSyncedFolderOf(tab, folder);
  }
}

void KoanWindowSyncClient::OrderItems(
    const std::vector<window_sync::ItemRecord>& target) {
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  int essential = 0;
  std::map<std::string, int> pinned_in_space;
  for (const window_sync::ItemRecord& record : target) {
    tabs::TabInterface* tab = FindItemTab(record.id);
    if (!tab || tab->IsSplit()) {
      continue;
    }
    const int index = model->GetIndexOfTab(tab);
    int to = TabStripModel::kNoTab;
    if (record.kind == window_sync::ItemRecord::Kind::kEssential) {
      if (!tab->IsPinned()) {
        continue;
      }
      to = essential++;
    } else {
      std::optional<tab_groups::TabGroupId> group =
          model->GetTabGroupForTab(index);
      if (!group || !model->group_model()->ContainsTabGroup(*group)) {
        continue;
      }
      // The Space's pinned tabs are at its top, in the target's order.
      to = static_cast<int>(
               model->group_model()->GetTabGroup(*group)->ListTabs().start()) +
           pinned_in_space[record.space_id]++;
    }
    if (index != to) {
      model->MoveWebContentsAt(index, to, /*select_after_move=*/false);
    }
  }
}

// --- TabStripModelObserver ---

void KoanWindowSyncClient::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // The user closed synced tabs (not the window's close, nor the sync's own
  // changes; a tab dragged to another window isn't deleted).
  if (change.type() == TabStripModelChange::kRemoved && !IsApplying() &&
      !tab_strip_model->closing_all()) {
    for (const TabStripModelChange::RemovedTab& removed :
         change.GetRemove()->contents) {
      std::string id = ItemIdOf(removed.contents);
      if (!id.empty() &&
          TabRemoveReasonUtils::WillDeleteWebContents(removed.remove_reason)) {
        closed_items_.insert(std::move(id));
      }
    }
  }
  if (change.type() != TabStripModelChange::kSelectionOnly) {
    ScheduleItemsReport();
  }
  // Zen's TabSelect: the selected synced tab's page comes here. Asked from a
  // task (KoanWindowSync::RequestShow()), never from this notification.
  if (selection.active_tab_changed() && !IsApplying()) {
    MaybeRequestActivePage();
  }
}

void KoanWindowSyncClient::OnTabChangedAt(tabs::TabInterface* tab,
                                          TabChangeType change_type) {
  const std::string item = ItemIdOf(tab->GetContents());
  if (change_type != TabChangeType::kAll || item.empty()) {
    return;
  }
  // A label, an icon or a pinned URL changed.
  ScheduleItemsReport();
  // The page's title or favicon: the other windows' rows show them.
  if (sync_ && !IsPlaceholder(tab->GetContents())) {
    sync_->OnPageChanged(this, item);
  }
}

void KoanWindowSyncClient::OnTabPinnedStateChanged(tabs::TabInterface* tab,
                                                   int index) {
  ScheduleItemsReport();
}

void KoanWindowSyncClient::TabGroupedStateChanged(
    TabStripModel* tab_strip_model,
    std::optional<tab_groups::TabGroupId> old_group,
    std::optional<tab_groups::TabGroupId> new_group,
    tabs::TabInterface* tab,
    int index) {
  ScheduleItemsReport();
}

}  // namespace koan
