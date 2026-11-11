// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_containers.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/common/referrer.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_session_data.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace koan::containers {

namespace {

// A restored tab whose pages OnRestoringContainerTab() replaced: it closes
// once it is in its window.
class BlankedContainerTab
    : public content::WebContentsUserData<BlankedContainerTab> {
 public:
  ~BlankedContainerTab() override = default;

 private:
  friend WebContentsUserData;
  explicit BlankedContainerTab(content::WebContents* contents)
      : WebContentsUserData(*contents) {}
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(BlankedContainerTab);

// Follows an agent's own tab in the user's partition (MarkAgentTab()): the
// tabs it opens, and its replacement on a discard, are the agent's too.
class AgentTabWatch : public content::WebContentsUserData<AgentTabWatch>,
                      public content::WebContentsObserver {
 public:
  ~AgentTabWatch() override = default;

  // content::WebContentsObserver:
  void DidOpenRequestedURL(content::WebContents* new_contents,
                           content::RenderFrameHost* source_render_frame_host,
                           const GURL& url,
                           const content::Referrer& referrer,
                           WindowOpenDisposition disposition,
                           ui::PageTransition transition,
                           bool started_from_context_menu,
                           bool renderer_initiated) override {
    MarkAgentTab(new_contents, task_);
  }
  void AboutToBeDiscarded(content::WebContents* new_contents) override {
    MarkAgentTab(new_contents, task_);
  }

 private:
  friend WebContentsUserData;
  AgentTabWatch(content::WebContents* contents, ContainerId task)
      : WebContentsUserData(*contents),
        content::WebContentsObserver(contents),
        task_(std::move(task)) {}
  WEB_CONTENTS_USER_DATA_KEY_DECL();

  const ContainerId task_;
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(AgentTabWatch);

void CloseBlankedTab(base::WeakPtr<content::WebContents> contents) {
  if (!contents) {
    return;
  }
  tabs::TabInterface* tab =
      tabs::TabInterface::MaybeGetFromContents(contents.get());
  if (!tab || !tab->GetBrowserWindowInterface()) {
    return;
  }
  TabStripModel* tab_strip =
      tab->GetBrowserWindowInterface()->GetTabStripModel();
  const int index = tab_strip->GetIndexOfWebContents(contents.get());
  // The window's only tab stays, blank: closing it would close the window
  // the user restored.
  if (index == TabStripModel::kNoTab || tab_strip->count() < 2) {
    return;
  }
  tab_strip->CloseWebContentsAt(index, TabCloseTypes::CLOSE_NONE);
}

}  // namespace

void OnTabInserted(content::WebContents* contents) {
  if (BlankedContainerTab::FromWebContents(contents)) {
    contents->RemoveUserData(BlankedContainerTab::UserDataKey());
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&CloseBlankedTab, contents->GetWeakPtr()));
  }
  const std::optional<ContainerId> container = ContainerOf(contents);
  const std::string data = container ? Serialize(*container) : std::string();
  if (GetTabSessionData(contents, kContainerSessionKey) != data) {
    SetTabSessionData(contents, kContainerSessionKey, data);
  }
  if (container && container->kind == ContainerKind::kAgent &&
      contents->GetController().IsInitialNavigation()) {
    // Not while the tab strip notifies its observers.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<content::WebContents> contents) {
                         if (contents) {
                           PlaceInContainerSpace(contents.get());
                         }
                       },
                       contents->GetWeakPtr()));
  }
}

void MarkAgentTab(content::WebContents* contents, const ContainerId& task) {
  if (!contents || task.kind != ContainerKind::kAgent ||
      AgentTabWatch::FromWebContents(contents)) {
    return;
  }
  SetAgentTab(contents, task);
  // A tab in a container is its container's (SetAgentTab() leaves it).
  if (ContainerOf(contents) != task) {
    return;
  }
  AgentTabWatch::CreateForWebContents(contents, task);
  // Opened by a click that the browser put in a window before the page heard
  // of it (OpenURL()): the session and the Space learn of it now.
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(contents);
  if (tab && tab->GetBrowserWindowInterface()) {
    OnTabInserted(contents);
  }
}

void OnRestoringContainerTab(
    content::WebContents* contents,
    const std::string& data,
    bool from_session_restore,
    std::vector<std::unique_ptr<content::NavigationEntry>>& entries,
    int& selected_navigation) {
  // A saved user container's tab that SiteInstanceForRestoredTab() created in
  // its container: its pages load there.
  const std::optional<ContainerId> container = Parse(data);
  if (container && container->kind == ContainerKind::kUser &&
      container->persistent() && ContainerOf(contents) == container) {
    return;
  }
  // Every other container's tab: an agent's never comes back (its task is
  // over), a throwaway user container's data is gone, and whatever `data`
  // says, the pages must not load in the wrong partition.
  entries.clear();
  entries.push_back(content::NavigationController::CreateNavigationEntry(
      GURL(url::kAboutBlankURL), content::Referrer(),
      /*initiator_origin=*/std::nullopt,
      /*initiator_base_url=*/std::nullopt, ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
      /*is_renderer_initiated=*/false, /*extra_headers=*/std::string(),
      contents->GetBrowserContext(),
      /*blob_url_loader_factory=*/nullptr));
  selected_navigation = 0;
  BlankedContainerTab::CreateForWebContents(contents);
}

void LoadSavedContainers(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return;
  }
  for (const auto [name, id] :
       profile->GetPrefs()->GetDict(prefs::kAgentSavedContainers)) {
    const std::string* text = id.GetIfString();
    if (!text) {
      continue;
    }
    const ContainerId container{.kind = ContainerKind::kAgent, .id = *text};
    if (IsValidContainerId(container.id) && container.persistent()) {
      profile->GetStoragePartition(PartitionConfigFor(profile, container),
                                   /*can_create=*/true);
    }
  }
}

}  // namespace koan::containers
