// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_agent_containers.h"

#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "base/functional/callback_helpers.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/supports_user_data.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/agent/koan_agent_policy.h"
#include "koan/browser/agent/koan_agent_tab_hooks.h"
#include "koan/browser/containers/koan_containers.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "ui/base/base_window.h"
#include "ui/base/mojom/window_show_state.mojom.h"
#include "ui/base/page_transition_types.h"

namespace koan::containers {

namespace {

// The Space's icon (an emoji, as the user's Spaces have).
constexpr char16_t kAgentSpaceIcon[] = u"\U0001F916";
constexpr char16_t kDefaultSpaceName[] = u"Agent";
// Saved names are what the user types in the prompt: short, printable.
constexpr size_t kMaxSavedNameLength = 64;

// The agent containers started in a profile and not yet ended, with their
// Space's name. A throwaway container that isn't here can't get new tabs.
class StartedContainers : public base::SupportsUserData::Data {
 public:
  static StartedContainers& For(Profile* profile) {
    auto* data = static_cast<StartedContainers*>(profile->GetUserData(Key()));
    if (!data) {
      auto owned = std::make_unique<StartedContainers>();
      data = owned.get();
      profile->SetUserData(Key(), std::move(owned));
    }
    return *data;
  }

  std::map<std::string, std::u16string>& names() { return names_; }
  // The containers agent control's tabs open in, by client and task
  // (ContainerTabOpener).
  std::map<std::pair<std::string, std::string>, ContainerId>& tasks() {
    return tasks_;
  }

 private:
  static const void* Key() {
    static const int kKey = 0;
    return &kKey;
  }

  std::map<std::string, std::u16string> names_;
  std::map<std::pair<std::string, std::string>, ContainerId> tasks_;
};

bool IsAllowedProfile(Profile* profile) {
  return profile && !profile->IsOffTheRecord() && profile->IsRegularProfile();
}

bool IsValidSavedName(const std::string& name) {
  if (name.empty() || name.size() > kMaxSavedNameLength ||
      !base::IsStringUTF8(name)) {
    return false;
  }
  for (char c : name) {
    if (base::IsAsciiControl(c)) {
      return false;
    }
  }
  return true;
}

// The saved container named `name`, created (and listed) if there is none.
ContainerId SavedContainerNamed(Profile* profile, const std::string& name) {
  ScopedDictPrefUpdate update(profile->GetPrefs(),
                              prefs::kAgentSavedContainers);
  if (const std::string* id = update->FindString(name)) {
    ContainerId container{.kind = ContainerKind::kAgent, .id = *id};
    if (IsValidContainerId(container.id) && container.persistent()) {
      return container;
    }
  }
  ContainerId container{.kind = ContainerKind::kAgent,
                        .id = NewContainerId(/*persistent=*/true)};
  update->Set(name, container.id);
  return container;
}

// Where the agent's Spaces go: the user's most recently active normal window
// of `profile` that has Spaces.
BrowserWindowInterface* FindWindow(Profile* profile) {
  BrowserWindowInterface* found = nullptr;
  ProfileBrowserCollection::GetForProfile(profile)->ForEach(
      [&](BrowserWindowInterface* browser) {
        if (browser->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
            HasSpaces(browser->GetTabStripModel())) {
          found = browser;
          return false;
        }
        return true;
      },
      BrowserCollection::Order::kActivation);
  return found;
}

// The Space `container`'s tabs are in, in `tab_strip`: the group of its first
// grouped tab other than `except`.
std::optional<tab_groups::TabGroupId> FindSpace(
    TabStripModel* tab_strip,
    const ContainerId& container,
    const content::WebContents* except = nullptr) {
  for (int i = 0; i < tab_strip->count(); ++i) {
    tabs::TabInterface* tab = tab_strip->GetTabAtIndex(i);
    if (tab->GetContents() != except && tab->GetGroup() &&
        ContainerOf(tab->GetContents()) == container) {
      return tab->GetGroup();
    }
  }
  return std::nullopt;
}

// The profile agents work in: the one of the user's most recently active
// normal window with Spaces, never off the record.
Profile* FindAgentProfile() {
  Profile* found = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* browser) {
        Profile* profile = browser->GetProfile();
        if (IsAllowedProfile(profile) &&
            browser->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
            HasSpaces(browser->GetTabStripModel())) {
          found = profile;
          return false;
        }
        return true;
      },
      BrowserCollection::Order::kActivation);
  return found;
}

// Opens a normal window for agents, in the background: on the screen of the
// window the user last used (any kind), so that macOS orders it behind that
// one instead of fronting it and switching Spaces
// (NativeWidgetNSWindowBridge::SetVisibilityState(), kShowInactive). It is
// never activated, and its active tab is the user's own new tab page in a
// Space of theirs; the agent's tabs go into Spaces of their own. Runs `ready`
// once the window is ready, or returns false (no profile agents may use, or
// Spaces are off).
bool OpenBackgroundWindow(base::OnceClosure ready) {
  Profile* profile = ProfileManager::GetLastUsedProfileIfLoaded();
  profile = profile ? profile->GetOriginalProfile() : nullptr;
  if (!IsAllowedProfile(profile) || !features::IsWorkspacesEnabled()) {
    return false;
  }
  BrowserWindowCreateParams params(BrowserWindowInterface::TYPE_NORMAL,
                                   *profile, /*from_user_gesture=*/false);
  params.initial_show_state = ui::mojom::WindowShowState::kInactive;
  if (BrowserWindowInterface* last =
          GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser()) {
    params.initial_bounds = last->GetWindow()->GetRestoredBounds();
  }
  CreateBrowserWindow(
      std::move(params),
      base::BindOnce(
          [](base::OnceClosure ready, BrowserWindowInterface* browser) {
            if (browser) {
              // A window always has a tab: the user's, not the agent's.
              chrome::AddTabAt(browser, GURL(), -1, /*foreground=*/true,
                               std::nullopt, /*pinned=*/false,
                               NavigateParams::WindowAction::kNoAction);
              browser->GetWindow()->ShowInactive();
            }
            std::move(ready).Run();
          },
          std::move(ready)));
  return true;
}

// Agent control's way to open a tab (koan_agent_tab_hooks.h): each client's
// task gets its own throwaway container and keeps it until EndAgentTask().
class ContainerTabOpener : public agent::AgentTabOpener {
 public:
  void OpenAgentTab(
      const agent::AgentTabRequest& request,
      base::OnceCallback<void(content::WebContents*)> done) override {
    if (!FindAgentProfile()) {
      // No normal window with Spaces (only Incognito, an app, or none): open
      // one in the background, once for every request waiting on it.
      waiting_.push_back({request, std::move(done)});
      if (waiting_.size() == 1 &&
          !OpenBackgroundWindow(base::BindOnce(
              // Never destroyed (InstallAgentTabOpener()).
              &ContainerTabOpener::OnWindowReady, base::Unretained(this)))) {
        OnWindowReady();
      }
      return;
    }
    Open(request, std::move(done));
  }

 private:
  struct Waiting {
    agent::AgentTabRequest request;
    base::OnceCallback<void(content::WebContents*)> done;
  };

  void OnWindowReady() {
    std::vector<Waiting> waiting = std::move(waiting_);
    waiting_.clear();
    for (Waiting& entry : waiting) {
      Open(entry.request, std::move(entry.done));
    }
  }

  static void Open(const agent::AgentTabRequest& request,
                   base::OnceCallback<void(content::WebContents*)> done) {
    Profile* profile = FindAgentProfile();
    std::optional<ContainerId> container =
        profile ? ContainerForTask(profile, request) : std::nullopt;
    std::move(done).Run(
        container ? containers::OpenAgentTab(profile, *container, request.url)
                  : nullptr);
  }

  static std::optional<ContainerId> ContainerForTask(
      Profile* profile,
      const agent::AgentTabRequest& request) {
    StartedContainers& started = StartedContainers::For(profile);
    const auto key = std::make_pair(request.client_id, request.task);
    if (auto it = started.tasks().find(key);
        it != started.tasks().end() &&
        started.names().contains(it->second.id)) {
      return it->second;
    }
    // Always a throwaway container. A saved one holds logins, so a task never
    // gets it by naming it (the task name is the agent's own string): only an
    // explicit, user-approved grant to that client may hand one out, and
    // there is none yet (maintainer review of #202).
    AgentContainerOptions options{
        .space_name = request.task.empty() ? std::u16string(kDefaultSpaceName)
                                           : base::UTF8ToUTF16(request.task)};
    auto container = StartAgentContainer(profile, options);
    if (!container.has_value()) {
      return std::nullopt;
    }
    started.tasks()[key] = *container;
    return *container;
  }

  std::vector<Waiting> waiting_;
};

}  // namespace

void PlaceInContainerSpace(content::WebContents* contents) {
  const std::optional<ContainerId> container = ContainerOf(contents);
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(contents);
  if (!container || !tab || !tab->GetBrowserWindowInterface()) {
    return;
  }
  TabStripModel* tab_strip =
      tab->GetBrowserWindowInterface()->GetTabStripModel();
  const std::optional<tab_groups::TabGroupId> space =
      FindSpace(tab_strip, *container, /*except=*/contents);
  const int index = tab_strip->GetIndexOfWebContents(contents);
  // No other tab of the container in this window: it stays where it opened.
  if (!space || index == TabStripModel::kNoTab || tab->GetGroup() == space) {
    return;
  }
  tab_strip->AddToExistingGroup({index}, *space, /*add_to_end=*/true);
}

void InstallAgentTabOpener() {
  static base::NoDestructor<ContainerTabOpener> opener;
  if (!agent::GetAgentTabOpener()) {
    agent::SetAgentTabOpener(opener.get());
  }
}

void EndAgentTask(const std::string& client_id, const std::string& task) {
  std::set<Profile*> profiles;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* browser) {
        profiles.insert(browser->GetProfile());
        return true;
      });
  for (Profile* profile : profiles) {
    if (!IsAllowedProfile(profile)) {
      continue;
    }
    StartedContainers& started = StartedContainers::For(profile);
    auto it = started.tasks().find(std::make_pair(client_id, task));
    if (it == started.tasks().end()) {
      continue;
    }
    const ContainerId container = it->second;
    started.tasks().erase(it);
    EndAgentContainer(profile, container);
  }
}

base::expected<ContainerId, AgentContainerError> StartAgentContainer(
    Profile* profile,
    const AgentContainerOptions& options) {
  if (!IsAllowedProfile(profile)) {
    return base::unexpected(AgentContainerError::kProfileNotAllowed);
  }
  if (!features::IsWorkspacesEnabled()) {
    return base::unexpected(AgentContainerError::kNoSpaces);
  }
  ContainerId container;
  switch (options.mode) {
    case AgentMode::kThrowaway:
      container = {.kind = ContainerKind::kAgent,
                   .id = NewContainerId(/*persistent=*/false)};
      break;
    case AgentMode::kSaved:
      if (!IsValidSavedName(options.saved_name)) {
        return base::unexpected(AgentContainerError::kInvalidName);
      }
      container = SavedContainerNamed(profile, options.saved_name);
      break;
    case AgentMode::kStrict:
      return base::unexpected(AgentContainerError::kNotSupported);
  }
  StartedContainers::For(profile).names()[container.id] =
      options.space_name.empty() ? std::u16string(kDefaultSpaceName)
                                 : options.space_name;
  return container;
}

content::WebContents* OpenAgentTab(Profile* profile,
                                   const ContainerId& container,
                                   const GURL& url) {
  if (!IsAllowedProfile(profile) || container.kind != ContainerKind::kAgent ||
      !agent::IsAllowedNavigationUrl(url)) {
    return nullptr;
  }
  std::map<std::string, std::u16string>& started =
      StartedContainers::For(profile).names();
  auto name = started.find(container.id);
  if (name == started.end()) {
    return nullptr;
  }
  BrowserWindowInterface* browser = FindWindow(profile);
  if (!browser) {
    return nullptr;
  }
  TabStripModel* tab_strip = browser->GetTabStripModel();

  content::WebContents::CreateParams create_params(
      profile, CreateSiteInstance(profile, container, url));
  // In the background: never the active tab, never shown.
  create_params.initially_hidden = true;
  std::unique_ptr<content::WebContents> owned =
      content::WebContents::Create(create_params);
  content::WebContents* contents = owned.get();
  content::NavigationController::LoadURLParams load(url);
  load.transition_type = ui::PAGE_TRANSITION_AUTO_TOPLEVEL;
  contents->GetController().LoadURLWithParams(load);

  const std::optional<tab_groups::TabGroupId> space =
      FindSpace(tab_strip, container);
  if (space) {
    const int end = static_cast<int>(
        tab_strip->group_model()->GetTabGroup(*space)->ListTabs().end());
    tab_strip->InsertWebContentsAt(end, std::move(owned), AddTabTypes::ADD_NONE,
                                   space);
    return contents;
  }
  const int index = tab_strip->InsertWebContentsAt(
      tab_strip->count(), std::move(owned), AddTabTypes::ADD_NONE);
  if (!CreateBackgroundSpace(tab_strip, index, name->second, kAgentSpaceIcon)) {
    // The window lost its Spaces in between: never leave the agent's tab
    // loose among the user's.
    tab_strip->CloseWebContentsAt(index, TabCloseTypes::CLOSE_NONE);
    return nullptr;
  }
  return contents;
}

std::vector<content::WebContents*> GetContainerTabs(
    Profile* profile,
    const ContainerId& container) {
  std::vector<content::WebContents*> tabs;
  if (!IsAllowedProfile(profile)) {
    return tabs;
  }
  ProfileBrowserCollection::GetForProfile(profile)->ForEach(
      [&](BrowserWindowInterface* browser) {
        TabStripModel* tab_strip = browser->GetTabStripModel();
        for (int i = 0; tab_strip && i < tab_strip->count(); ++i) {
          content::WebContents* contents = tab_strip->GetWebContentsAt(i);
          if (ContainerOf(contents) == container) {
            tabs.push_back(contents);
          }
        }
        return true;
      });
  return tabs;
}

void EndAgentContainer(Profile* profile, const ContainerId& container) {
  if (!IsAllowedProfile(profile) || container.kind != ContainerKind::kAgent) {
    return;
  }
  StartedContainers::For(profile).names().erase(container.id);
  for (content::WebContents* contents : GetContainerTabs(profile, container)) {
    tabs::TabInterface* tab = tabs::TabInterface::GetFromContents(contents);
    TabStripModel* tab_strip =
        tab->GetBrowserWindowInterface()->GetTabStripModel();
    tab_strip->CloseWebContentsAt(tab_strip->GetIndexOfWebContents(contents),
                                  TabCloseTypes::CLOSE_NONE);
  }
  if (container.persistent()) {
    return;
  }
  // An in-memory partition lives as long as the profile (browser_context.h);
  // what it held goes now. It is never handed to another task.
  if (content::StoragePartition* partition = profile->GetStoragePartition(
          PartitionConfigFor(profile, container), /*can_create=*/false)) {
    partition->ClearData(content::StoragePartition::REMOVE_DATA_MASK_ALL,
                         blink::StorageKey(), base::Time(), base::Time::Max(),
                         base::DoNothing());
  }
}

std::vector<SavedContainer> GetSavedAgentContainers(Profile* profile) {
  std::vector<SavedContainer> saved;
  if (!IsAllowedProfile(profile)) {
    return saved;
  }
  for (const auto [name, id] :
       profile->GetPrefs()->GetDict(prefs::kAgentSavedContainers)) {
    const std::string* text = id.GetIfString();
    if (text && IsValidContainerId(*text) && text->front() == 's') {
      saved.push_back(
          {.name = name, .id = {.kind = ContainerKind::kAgent, .id = *text}});
    }
  }
  return saved;
}

void DeleteSavedAgentContainer(Profile* profile, const std::string& name) {
  if (!IsAllowedProfile(profile)) {
    return;
  }
  std::optional<ContainerId> container;
  {
    ScopedDictPrefUpdate update(profile->GetPrefs(),
                                prefs::kAgentSavedContainers);
    if (const std::string* id = update->FindString(name)) {
      if (IsValidContainerId(*id)) {
        container = ContainerId{.kind = ContainerKind::kAgent, .id = *id};
      }
    }
    update->Remove(name);
  }
  if (!container) {
    return;
  }
  EndAgentContainer(profile, *container);
  // Clears a loaded partition and deletes its folder; a folder in use is left
  // for the next start, when nothing loads it and Chromium's storage
  // partition garbage collection (which no longer keeps it) takes it.
  profile->AsyncObliterateStoragePartition(
      PartitionConfigFor(profile, *container).partition_domain(),
      base::DoNothing(), base::DoNothing());
}

}  // namespace koan::containers
