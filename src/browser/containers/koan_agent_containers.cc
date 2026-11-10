// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_agent_containers.h"

#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/supports_user_data.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
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
#include "koan/browser/containers/koan_container_grants.h"
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

 private:
  static const void* Key() {
    static const int kKey = 0;
    return &kKey;
  }

  std::map<std::string, std::u16string> names_;
};

// A client's task (AgentTabRequest's client id and task).
using TaskKey = std::pair<std::string, std::string>;

// The containers agent control's tabs open in, by task, in whichever profile
// each is (the user's, or a strict task's hidden one).
std::map<TaskKey, AgentContainer>& Tasks() {
  static base::NoDestructor<std::map<TaskKey, AgentContainer>> tasks;
  return *tasks;
}

// Tasks whose container is starting, and those of them that ended meanwhile
// (EndAgentTask()): an ended one's container is ended as soon as it starts.
std::set<TaskKey>& StartingTasks() {
  static base::NoDestructor<std::set<TaskKey>> tasks;
  return *tasks;
}
std::set<TaskKey>& EndedWhileStarting() {
  static base::NoDestructor<std::set<TaskKey>> tasks;
  return *tasks;
}

// Strict tasks' hidden profiles, each kept alive until its task ends.
std::map<base::FilePath, std::unique_ptr<ScopedProfileKeepAlive>>&
StrictProfiles() {
  static base::NoDestructor<
      std::map<base::FilePath, std::unique_ptr<ScopedProfileKeepAlive>>>
      profiles;
  return *profiles;
}

bool IsStrictProfile(Profile* profile) {
  return StrictProfiles().contains(profile->GetPath());
}

// The requests waiting for a profile's background window, by profile.
std::map<base::FilePath, std::vector<base::OnceClosure>>& WindowWaiters() {
  static base::NoDestructor<
      std::map<base::FilePath, std::vector<base::OnceClosure>>>
      waiters;
  return *waiters;
}

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
        if (IsAllowedProfile(profile) && !IsStrictProfile(profile) &&
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

// Opens a normal window of `profile` for agents, in the background: on the
// screen of the window the user last used (any kind), so that macOS orders it
// behind that one instead of fronting it and switching Spaces
// (NativeWidgetNSWindowBridge::SetVisibilityState(), kShowInactive). It is
// never activated, and its active tab is a new tab page in a Space of its own;
// the agent's tabs go into Spaces of their own. Runs `ready` once it is
// ready, or at once if it can't be opened.
void OpenBackgroundWindow(Profile* profile, base::OnceClosure ready) {
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
              // A window always has a tab, and it isn't the agent's.
              chrome::AddTabAt(browser, GURL(), -1, /*foreground=*/true,
                               std::nullopt, /*pinned=*/false,
                               NavigateParams::WindowAction::kNoAction);
              browser->GetWindow()->ShowInactive();
            }
            std::move(ready).Run();
          },
          std::move(ready)));
}

// The profile a new task starts in: the user's most recently active one with
// a Spaces window, else the last used one (its window opens in the
// background).
Profile* TaskProfile() {
  if (Profile* profile = FindAgentProfile()) {
    return profile;
  }
  Profile* profile = ProfileManager::GetLastUsedProfileIfLoaded();
  profile = profile ? profile->GetOriginalProfile() : nullptr;
  return IsAllowedProfile(profile) && !IsStrictProfile(profile) ? profile
                                                                : nullptr;
}

// Agent control's way to open a tab (koan_agent_tab_hooks.h): each client's
// task gets its own container (throwaway, strict, or a saved one granted to
// the client) and keeps it until EndAgentTask().
class ContainerTabOpener : public agent::AgentTabOpener {
 public:
  void OpenAgentTab(
      const agent::AgentTabRequest& request,
      base::OnceCallback<void(content::WebContents*)> done) override {
    const TaskKey key(request.client_id, request.task);
    if (auto it = Tasks().find(key); it != Tasks().end()) {
      OpenInTask(it->second, request.url, std::move(done));
      return;
    }
    // The task's container is starting: its tabs wait for it, so that a task
    // never gets two.
    Starting& starting = starting_[key];
    starting.waiting.push_back({request.url, std::move(done)});
    if (starting.waiting.size() > 1) {
      return;
    }
    StartingTasks().insert(key);
    Profile* profile = TaskProfile();
    std::optional<AgentContainerOptions> options =
        profile ? OptionsForTask(profile, request, &starting.saved_id)
                : std::nullopt;
    if (!options) {
      OnStarted(key, base::unexpected(AgentContainerError::kProfileNotAllowed));
      return;
    }
    StartAgentContainer(profile, *options,
                        // Never destroyed (InstallAgentTabOpener()).
                        base::BindOnce(&ContainerTabOpener::OnStarted,
                                       base::Unretained(this), key));
  }

 private:
  struct Waiting {
    GURL url;
    base::OnceCallback<void(content::WebContents*)> done;
  };
  struct Starting {
    // The saved container the task is starting, if any.
    std::string saved_id;
    std::vector<Waiting> waiting;
  };

  void OnStarted(const TaskKey& key, AgentContainerResult result) {
    std::vector<Waiting> waiting = std::move(starting_[key].waiting);
    starting_.erase(key);
    StartingTasks().erase(key);
    if (EndedWhileStarting().erase(key) && result.has_value() &&
        result->profile) {
      EndAgentContainer(result->profile.get(), result->id);
      result = base::unexpected(AgentContainerError::kProfileNotAllowed);
    }
    if (!result.has_value() || !result->profile) {
      for (Waiting& entry : waiting) {
        std::move(entry.done).Run(nullptr);
      }
      return;
    }
    Tasks()[key] = *result;
    for (Waiting& entry : waiting) {
      OpenInTask(*result, entry.url, std::move(entry.done));
    }
  }

  // Opens `url` in the task's container, once its profile has a window.
  static void OpenInTask(const AgentContainer& container,
                         const GURL& url,
                         base::OnceCallback<void(content::WebContents*)> done) {
    if (!container.profile) {
      std::move(done).Run(nullptr);
      return;
    }
    EnsureAgentWindow(
        container.profile.get(),
        base::BindOnce(
            [](AgentContainer container, GURL url,
               base::OnceCallback<void(content::WebContents*)> done) {
              std::move(done).Run(
                  container.profile
                      ? containers::OpenAgentTab(container.profile.get(),
                                                 container.id, url)
                      : nullptr);
            },
            container, url, std::move(done)));
  }

  // What a new task starts, or nullopt if it may not; `saved_id` is set to
  // the saved container it gets, if any.
  std::optional<AgentContainerOptions> OptionsForTask(
      Profile* profile,
      const agent::AgentTabRequest& request,
      std::string* saved_id) const {
    // A saved container holds logins, so naming one is never enough (the
    // name is the agent's own string): the user must have granted it to this
    // client (maintainer review of #202). A refused request opens nothing
    // rather than a throwaway tab the agent didn't ask for.
    std::map<std::string, std::string, std::less<>> saved;
    for (const SavedContainer& container : GetSavedAgentContainers(profile)) {
      saved[container.name] = container.id.id;
    }
    std::set<std::string, std::less<>> in_use;
    for (const auto& [task, container] : Tasks()) {
      in_use.insert(container.id.id);
    }
    for (const auto& [task, starting] : starting_) {
      if (!starting.saved_id.empty()) {
        in_use.insert(starting.saved_id);
      }
    }
    const ContainerChoice choice =
        ChooseContainer(ContainerGrants::Get(), request.client_id,
                        request.saved_container, saved, in_use);
    if (choice.kind == ContainerChoice::Kind::kRefused) {
      return std::nullopt;
    }
    AgentContainerOptions options{
        .space_name = request.task.empty() ? std::u16string(kDefaultSpaceName)
                                           : base::UTF8ToUTF16(request.task)};
    if (choice.kind == ContainerChoice::Kind::kSaved) {
      options.mode = AgentMode::kSaved;
      options.saved_name = request.saved_container;
      *saved_id = choice.container_id;
    } else if (profile->GetPrefs()->GetBoolean(prefs::kAgentControlStrict)) {
      options.mode = AgentMode::kStrict;
    }
    return options;
  }

  std::map<TaskKey, Starting> starting_;
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
  const TaskKey key(client_id, task);
  auto it = Tasks().find(key);
  if (it == Tasks().end()) {
    if (StartingTasks().contains(key)) {
      EndedWhileStarting().insert(key);
    }
    return;
  }
  const AgentContainer container = it->second;
  Tasks().erase(it);
  if (container.profile) {
    EndAgentContainer(container.profile.get(), container.id);
  }
}

namespace {

// Records a started container of `profile` and its Space's name, and runs
// `done` with it, asynchronously.
void FinishStart(Profile* profile,
                 const ContainerId& container,
                 const std::u16string& space_name,
                 base::OnceCallback<void(AgentContainerResult)> done) {
  StartedContainers::For(profile).names()[container.id] =
      space_name.empty() ? std::u16string(kDefaultSpaceName) : space_name;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(done),
                     AgentContainerResult(AgentContainer{
                         .profile = profile->GetWeakPtr(), .id = container})));
}

// kStrict, once the hidden profile is created and initialised.
void OnStrictProfileReady(std::u16string space_name,
                          base::OnceCallback<void(AgentContainerResult)> done,
                          Profile* profile) {
  if (!IsAllowedProfile(profile)) {
    std::move(done).Run(base::unexpected(AgentContainerError::kProfileFailed));
    return;
  }
  // CreateMultiProfileAsync(is_hidden) made it ephemeral (deleted at the
  // next start if Koan doesn't get to it) and omitted (in no profile list).
  // Omitted again explicitly: the launch picker and menus must never show it.
  if (ProfileAttributesEntry* entry =
          g_browser_process->profile_manager()
              ->GetProfileAttributesStorage()
              .GetProfileAttributesWithPath(profile->GetPath())) {
    entry->SetIsOmitted(true);
  }
  // Nobody uses it but the agent: no welcome screen in its window.
  profile->GetPrefs()->SetBoolean(prefs::kWelcomeScreenSeen, true);
  // Alive until the task ends; then, with its windows closed, Chromium
  // deletes it (ProfileManager::RemoveKeepAlive() schedules an ephemeral
  // profile's deletion with its last keep-alive).
  StrictProfiles()[profile->GetPath()] =
      std::make_unique<ScopedProfileKeepAlive>(
          profile, ProfileKeepAliveOrigin::kRemoteDebugging);
  FinishStart(profile,
              {.kind = ContainerKind::kAgent,
               .id = NewContainerId(/*persistent=*/false)},
              space_name, std::move(done));
}

}  // namespace

void StartAgentContainer(Profile* profile,
                         const AgentContainerOptions& options,
                         base::OnceCallback<void(AgentContainerResult)> done) {
  auto fail = [&](AgentContainerError error) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(done), base::unexpected(error)));
  };
  if (!IsAllowedProfile(profile) || IsStrictProfile(profile)) {
    fail(AgentContainerError::kProfileNotAllowed);
    return;
  }
  if (!features::IsWorkspacesEnabled()) {
    fail(AgentContainerError::kNoSpaces);
    return;
  }
  switch (options.mode) {
    case AgentMode::kThrowaway:
      FinishStart(profile,
                  {.kind = ContainerKind::kAgent,
                   .id = NewContainerId(/*persistent=*/false)},
                  options.space_name, std::move(done));
      return;
    case AgentMode::kSaved:
      if (!IsValidSavedName(options.saved_name)) {
        fail(AgentContainerError::kInvalidName);
        return;
      }
      FinishStart(profile, SavedContainerNamed(profile, options.saved_name),
                  options.space_name, std::move(done));
      return;
    case AgentMode::kStrict:
      // An agent container inside the hidden profile, so that every agent
      // block still applies there too.
      ProfileManager::CreateMultiProfileAsync(
          u"Koan agent", /*icon_index=*/0, /*is_hidden=*/true,
          base::BindOnce(&OnStrictProfileReady, options.space_name,
                         std::move(done)));
      return;
  }
}

void EnsureAgentWindow(Profile* profile, base::OnceClosure ready) {
  if (!IsAllowedProfile(profile) || FindWindow(profile)) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                             std::move(ready));
    return;
  }
  std::vector<base::OnceClosure>& waiters = WindowWaiters()[profile->GetPath()];
  waiters.push_back(std::move(ready));
  if (waiters.size() > 1) {
    return;
  }
  OpenBackgroundWindow(profile, base::BindOnce(
                                    [](base::FilePath path) {
                                      std::vector<base::OnceClosure> waiting =
                                          std::move(WindowWaiters()[path]);
                                      WindowWaiters().erase(path);
                                      for (base::OnceClosure& ready : waiting) {
                                        std::move(ready).Run();
                                      }
                                    },
                                    profile->GetPath()));
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
  if (IsStrictProfile(profile)) {
    // The task's hidden profile: close its windows and let go of it. Its
    // in-memory container goes with it, as Chromium deletes the profile.
    ProfileBrowserCollection::GetForProfile(profile)->ForEach(
        [](BrowserWindowInterface* browser) {
          browser->GetWindow()->Close();
          return true;
        });
    StrictProfiles().erase(profile->GetPath());
    return;
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
  ContainerGrants::Get().RemoveContainer(container->id);
  EndAgentContainer(profile, *container);
  // Clears a loaded partition and deletes its folder; a folder in use is left
  // for the next start, when nothing loads it and Chromium's storage
  // partition garbage collection (which no longer keeps it) takes it.
  profile->AsyncObliterateStoragePartition(
      PartitionConfigFor(profile, *container).partition_domain(),
      base::DoNothing(), base::DoNothing());
}

bool CreateSavedAgentContainer(Profile* profile, const std::string& name) {
  if (!IsAllowedProfile(profile) || !IsValidSavedName(name) ||
      profile->GetPrefs()
          ->GetDict(prefs::kAgentSavedContainers)
          .FindString(name)) {
    return false;
  }
  SavedContainerNamed(profile, name);
  return true;
}

}  // namespace koan::containers
