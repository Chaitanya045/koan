// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_window_sync_client.h"

#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/token.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/sessions/core/session_id.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_session_data.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/gfx/range/range.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace koan {

namespace {

// The window's session data (koan_session_data.h): {"<tab group token>":
// "<Space sync id>"}.
constexpr char kSpaceIdsSessionKey[] = "koan.window_sync.spaces";

std::string NewSyncId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

KoanWorkspaceController::SpaceData ToSpaceData(
    const window_sync::SpaceRecord& record) {
  return {.name = record.name,
          .icon = record.icon,
          .theme = {.stored = record.theme}};
}

}  // namespace

KoanWindowSyncClient::KoanWindowSyncClient(BrowserView* browser_view)
    : browser_view_(browser_view) {}

KoanWindowSyncClient::~KoanWindowSyncClient() {
  OnBrowserViewDestroying();
}

// static
KoanWindowSyncClient* KoanWindowSyncClient::From(
    const BrowserView* browser_view) {
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  KoanWindowSyncClient* client =
      controller ? controller->window_sync() : nullptr;
  return client && client->is_synced() ? client : nullptr;
}

void KoanWindowSyncClient::OnAddedToWidget() {
  if (sync_ || session_restored_subscription_) {
    return;
  }
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  // Zen syncs windows with Spaces only (gZenWorkspaces.
  // privateWindowOrDisabled is never synced).
  if (!controller || !controller->workspaces()->IsEnabled() ||
      !KoanWindowSync::ShouldSync(browser_view_)) {
    return;
  }
  // A window a session restore is filling joins once it has its Spaces:
  // before, it would take the others' Spaces and then get its own on top.
  if (SessionRestore::IsRestoring(browser_view_->GetProfile())) {
    session_restored_subscription_ =
        SessionRestore::RegisterOnSessionRestoredCallback(base::BindRepeating(
            &KoanWindowSyncClient::OnSessionRestored, base::Unretained(this)));
    return;
  }
  Join();
}

void KoanWindowSyncClient::OnSessionRestored(Profile* profile, int tab_count) {
  if (profile != browser_view_->GetProfile() ||
      SessionRestore::IsRestoring(profile)) {
    return;
  }
  session_restored_subscription_ = base::CallbackListSubscription();
  Join();
}

void KoanWindowSyncClient::Join() {
  sync_ = KoanWindowSync::ForProfile(browser_view_->GetProfile());
  if (!sync_) {
    return;
  }
  if (views::Widget* widget = browser_view_->GetWidget()) {
    widget_observation_.Observe(widget);
  }
  LoadSpaceIds();
  spaces_changed_subscription_ =
      KoanBrowserViewController::From(browser_view_)
          ->workspaces()
          ->AddSpacesChangedCallback(
              base::BindRepeating(&KoanWindowSyncClient::OnLocalSpacesChanged,
                                  weak_factory_.GetWeakPtr()));
  sync_->AddWindow(this);
  if (widget_observation_.IsObserving() &&
      widget_observation_.GetSource()->IsActive()) {
    sync_->OnWindowActivated(this);
  }
  // A first window seeds the model with what it has (a later one is
  // reconciled first; its report is ignored until then).
  OnLocalSpacesChanged();
}

void KoanWindowSyncClient::OnBrowserViewDestroying() {
  session_restored_subscription_ = base::CallbackListSubscription();
  spaces_changed_subscription_ = base::CallbackListSubscription();
  widget_observation_.Reset();
  if (sync_) {
    sync_->RemoveWindow(this);
    sync_ = nullptr;
  }
}

bool KoanWindowSyncClient::IsApplying() const {
  return sync_ && sync_->IsApplying(this);
}

KoanWindowSync::WindowId KoanWindowSyncClient::window_id() const {
  return browser_view_->browser()->GetSessionID().id();
}

void KoanWindowSyncClient::OnWidgetActivationChanged(views::Widget* widget,
                                                     bool active) {
  if (active && sync_) {
    sync_->OnWindowActivated(this);
  }
}

void KoanWindowSyncClient::OnWidgetDestroying(views::Widget* widget) {
  widget_observation_.Reset();
}

// --- Spaces ---

std::vector<window_sync::SpaceRecord> KoanWindowSyncClient::LocalSpaces(
    bool assign_ids,
    std::vector<tab_groups::TabGroupId>* unmapped) {
  KoanWorkspaceController* workspaces =
      KoanBrowserViewController::From(browser_view_)->workspaces();
  TabStripModel* model = workspaces->tab_strip_model();
  std::vector<window_sync::SpaceRecord> records;
  std::map<tab_groups::TabGroupId, std::string> ids;
  bool assigned = false;
  for (const KoanWorkspaceController::SpaceInfo& space :
       workspaces->GetSpaces()) {
    // An agent container's Space (its tabs are in the container) never
    // syncs: agent tabs stay out of every other window.
    bool agent = false;
    if (model->group_model()->ContainsTabGroup(space.id)) {
      const gfx::Range range =
          model->group_model()->GetTabGroup(space.id)->ListTabs();
      for (size_t i = range.start(); i < range.end(); ++i) {
        content::WebContents* contents =
            model->GetWebContentsAt(static_cast<int>(i));
        if (contents && containers::IsAgentContainerContents(contents)) {
          agent = true;
          break;
        }
      }
    }
    if (agent) {
      continue;
    }
    auto it = space_ids_.find(space.id);
    std::string id = it != space_ids_.end() ? it->second : std::string();
    if (id.empty()) {
      if (!assign_ids) {
        if (unmapped) {
          unmapped->push_back(space.id);
        }
        continue;
      }
      id = NewSyncId();
      assigned = true;
    }
    ids[space.id] = id;
    records.push_back({.id = std::move(id),
                       .name = space.name,
                       .icon = space.icon,
                       .theme = space.theme.stored});
  }
  // Groups that are gone (or are an agent's) lose their id.
  if (assign_ids && (assigned || ids.size() != space_ids_.size())) {
    space_ids_ = std::move(ids);
    SaveSpaceIds();
  }
  return records;
}

std::optional<tab_groups::TabGroupId> KoanWindowSyncClient::FindSpace(
    const std::string& id) const {
  for (const auto& [group, sync_id] : space_ids_) {
    if (sync_id == id) {
      return group;
    }
  }
  return std::nullopt;
}

void KoanWindowSyncClient::OnLocalSpacesChanged() {
  // What the sync does here isn't the user's; an out-of-date window's state
  // is stale (KoanWindowSync ignores both, and ids aren't handed out for
  // Spaces the reconcile will adopt).
  if (!sync_ || !sync_->IsInSync(this)) {
    return;
  }
  sync_->ReportSpaces(this, LocalSpaces(/*assign_ids=*/true));
}

void KoanWindowSyncClient::ApplySpaces(
    const std::vector<window_sync::SpaceRecord>& target) {
  KoanWorkspaceController* workspaces =
      KoanBrowserViewController::From(browser_view_)->workspaces();
  TabStripModel* model = workspaces->tab_strip_model();
  if (target.empty() || model->closing_all()) {
    return;
  }
  // A joining window's own Spaces become synced ones, with their tabs
  // (window_sync::AdoptSpaces()); what is left keeps Spaces of its own,
  // which it reports afterwards, so the other windows get them too.
  std::vector<tab_groups::TabGroupId> unmapped;
  std::vector<window_sync::SpaceRecord> local =
      LocalSpaces(/*assign_ids=*/false, &unmapped);
  if (!unmapped.empty()) {
    std::set<std::string> taken;
    for (const window_sync::SpaceRecord& record : local) {
      taken.insert(record.id);
    }
    std::vector<window_sync::SpaceRecord> unmapped_records;
    for (const tab_groups::TabGroupId& group : unmapped) {
      std::optional<KoanWorkspaceController::SpaceInfo> info =
          workspaces->GetSpace(group);
      unmapped_records.push_back(
          {.name = info ? info->name : std::u16string(),
           .icon = info ? info->icon : std::u16string()});
    }
    const std::vector<std::string> adopted =
        window_sync::AdoptSpaces(unmapped_records, target, taken);
    for (size_t i = 0; i < unmapped.size(); ++i) {
      if (!adopted[i].empty()) {
        space_ids_[unmapped[i]] = adopted[i];
      }
    }
    local = LocalSpaces(/*assign_ids=*/false);
  }

  const window_sync::Plan<window_sync::SpaceRecord> plan =
      window_sync::PlanSpaces(local, target);
  // Additions first: a window keeps at least one Space.
  for (const auto& [record, index] : plan.add) {
    content::WebContents* contents = chrome::AddAndReturnTabAt(
        browser_view_->browser(), GURL(), model->count(), /*foreground=*/false);
    const int tab_index = model->GetIndexOfWebContents(contents);
    if (tab_index == TabStripModel::kNoTab) {
      continue;
    }
    space_ids_[workspaces->CreateBackgroundSpace(
        tab_index, ToSpaceData(record))] = record.id;
  }
  for (const window_sync::SpaceRecord& record : plan.update) {
    if (std::optional<tab_groups::TabGroupId> group = FindSpace(record.id)) {
      workspaces->WriteSpace(*group, ToSpaceData(record));
    }
  }
  for (const std::string& id : plan.remove) {
    if (std::optional<tab_groups::TabGroupId> group = FindSpace(id)) {
      workspaces->DeleteSpaceForSync(*group);
      space_ids_.erase(*group);
    }
  }
  // The target order (Spaces of this window's own, an agent's, go after).
  if (plan.order || !plan.add.empty()) {
    for (size_t i = 0; i < target.size(); ++i) {
      if (std::optional<tab_groups::TabGroupId> group =
              FindSpace(target[i].id)) {
        workspaces->MoveSpace(*group, static_cast<int>(i));
      }
    }
  }
  SaveSpaceIds();
}

void KoanWindowSyncClient::LoadSpaceIds() {
  space_ids_.clear();
  std::optional<base::Value> value = base::JSONReader::Read(
      GetWindowSessionData(browser_view_->browser(), kSpaceIdsSessionKey),
      base::JSON_PARSE_RFC);
  if (!value || !value->is_dict()) {
    return;
  }
  for (const auto [token, id] : value->GetDict()) {
    std::optional<base::Token> parsed = base::Token::FromString(token);
    if (parsed && !parsed->is_zero() && id.is_string()) {
      space_ids_[tab_groups::TabGroupId::FromRawToken(*parsed)] =
          id.GetString();
    }
  }
}

void KoanWindowSyncClient::SaveSpaceIds() {
  base::DictValue dict;
  for (const auto& [group, id] : space_ids_) {
    dict.Set(group.ToString(), id);
  }
  SetWindowSessionData(browser_view_->browser(), kSpaceIdsSessionKey,
                       base::WriteJson(dict).value_or(std::string()));
}

}  // namespace koan
