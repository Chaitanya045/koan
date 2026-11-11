// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_forwarder.h"

#include <set>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/rand_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/time/default_tick_clock.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "components/embedder_support/user_agent_utils.h"
#include "components/security_interstitials/content/security_interstitial_tab_helper.h"
#include "components/tabs/public/tab_interface.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_observer.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "koan/browser/agent/koan_agent_devtools_client.h"
#include "koan/browser/agent/koan_agent_facade.h"
#include "koan/browser/agent/koan_agent_lease.h"
#include "koan/browser/agent/koan_agent_page_surfaces.h"
#include "koan/browser/agent/koan_agent_reach_content.h"
#include "koan/browser/agent/koan_agent_service.h"
#include "koan/browser/agent/koan_agent_tab_hooks.h"
#include "koan/browser/containers/koan_agent_access.h"
#include "koan/browser/containers/koan_agent_containers.h"
#include "net/base/schemeful_site.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "url/origin.h"

namespace koan::agent {

namespace {

AuditLog::ChainKey NewChainKey() {
  // Per launch. Binding it to the Keychain (design section 6.3) comes with
  // the Keychain-bound client store; until then a launch's chain shows an edit
  // within that launch only.
  AuditLog::ChainKey key;
  base::RandBytes(key);
  return key;
}

std::string TargetIdFor(content::WebContents* contents) {
  scoped_refptr<content::DevToolsAgentHost> host =
      content::DevToolsAgentHost::GetOrCreateFor(contents);
  return host ? host->GetId() : std::string();
}

// How long the user has to answer a take-over before it is a no.
constexpr base::TimeDelta kTakeOverTimeout = base::Seconds(60);

// Input the user made, not the agent: every event CDP injects carries
// kFromDebugger.
bool IsUsersInput(const blink::WebInputEvent& event) {
  return !(event.GetModifiers() & blink::WebInputEvent::kFromDebugger);
}

}  // namespace

std::string SiteFor(const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return std::string();
  }
  return net::SchemefulSite(url).Serialize();
}

// Watches a tab in the registry: its end, the changes the facades announce,
// the user's own input in it (which pauses the agent, design section 9.2), and
// a user tab leaving the site the user allowed.
class AgentForwarder::TabWatcher final
    : public content::WebContentsObserver,
      public content::RenderWidgetHost::InputEventObserver,
      public content::RenderWidgetHostObserver {
 public:
  TabWatcher(AgentForwarder* forwarder,
             content::WebContents* contents,
             std::string target_id)
      : content::WebContentsObserver(contents),
        forwarder_(forwarder),
        target_id_(std::move(target_id)) {
    // A wheel scroll is not a "user interaction" upstream, and an observer on
    // the root widget alone would miss a cross-origin frame's (a payment
    // widget's), so every frame's widget is watched for it.
    contents->ForEachRenderFrameHost(
        [this](content::RenderFrameHost* frame) { Watch(frame); });
  }

  ~TabWatcher() override {
    for (content::RenderWidgetHost* widget : widgets_) {
      widget->RemoveInputEventObserver(this);
      widget->RemoveObserver(this);
    }
  }

  // content::WebContentsObserver:
  void WebContentsDestroyed() override {
    // Destroys this.
    forwarder_->OnTabDestroyed(std::string(target_id_));
  }
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (handle->IsInPrimaryMainFrame() && handle->HasCommitted()) {
      forwarder_->OnTabNavigated(target_id_, handle->GetURL());
      forwarder_->OnTabChanged(target_id_);
    }
  }
  void TitleWasSetForMainFrame(content::RenderFrameHost*) override {
    forwarder_->OnTabChanged(target_id_);
  }
  void RenderFrameCreated(content::RenderFrameHost* frame) override {
    Watch(frame);
  }
  // Mouse down, key down, the start of a scroll, a touch: in any frame of the
  // tab. Switching to the tab is not input; the omnibox and the tab strip
  // never reach here.
  void DidGetUserInteraction(const blink::WebInputEvent& event) override {
    if (IsUsersInput(event)) {
      forwarder_->OnUserInput(target_id_);
    }
  }

  // content::RenderWidgetHost::InputEventObserver:
  void OnInputEvent(const content::RenderWidgetHost& widget,
                    const blink::WebInputEvent& event,
                    InputEventSource source) override {
    if (event.GetType() == blink::WebInputEvent::Type::kMouseWheel &&
        IsUsersInput(event)) {
      forwarder_->OnUserInput(target_id_);
    }
  }

  // content::RenderWidgetHostObserver:
  void RenderWidgetHostDestroyed(content::RenderWidgetHost* widget) override {
    widget->RemoveInputEventObserver(this);
    widget->RemoveObserver(this);
    widgets_.erase(widget);
  }

 private:
  void Watch(content::RenderFrameHost* frame) {
    content::RenderWidgetHost* widget = frame->GetRenderWidgetHost();
    if (widget && widgets_.insert(widget).second) {
      widget->AddInputEventObserver(this);
      widget->AddObserver(this);
    }
  }

  const raw_ptr<AgentForwarder> forwarder_;
  const std::string target_id_;
  std::set<raw_ptr<content::RenderWidgetHost>> widgets_;
};

// One authenticated connection: its facade, and the DevTools client the facade
// has on each tab it attached.
class AgentForwarder::Connection final : public FacadeBackend {
 public:
  Connection(AgentForwarder* forwarder,
             uint64_t connection_id,
             std::string client_id)
      : forwarder_(forwarder), client_id_(client_id) {
    facade_ = std::make_unique<CdpFacade>(
        connection_id, std::move(client_id), &forwarder->registry_, this,
        base::DefaultTickClock::GetInstance(),
        base::BindRepeating(forwarder->send_, connection_id));
  }

  ~Connection() override {
    // The facade detaches its tabs through this backend.
    facade_.reset();
    clients_.clear();
  }

  CdpFacade& facade() { return *facade_; }
  const std::string& client_id() const { return client_id_; }

  // FacadeBackend:
  std::optional<TabFacts> DescribeTab(std::string_view target_id) override {
    content::WebContents* contents = forwarder_->ContentsFor(target_id);
    if (!contents) {
      return std::nullopt;
    }
    TabFacts facts{base::UTF16ToUTF8(contents->GetTitle()),
                   contents->GetLastCommittedURL()};
    if (auto* interstitials = security_interstitials::
            SecurityInterstitialTabHelper::FromWebContents(contents)) {
      facts.interstitial = interstitials->IsDisplayingInterstitial();
    }
    return facts;
  }

  bool AttachTab(const std::string& target_id) override {
    content::WebContents* contents = forwarder_->ContentsFor(target_id);
    if (!contents) {
      return false;
    }
    auto client = std::make_unique<AgentDevToolsClient>(
        contents,
        base::BindRepeating(&Connection::OnTabMessage,
                            weak_factory_.GetWeakPtr(), target_id),
        base::BindOnce(&Connection::OnTabClosed, weak_factory_.GetWeakPtr(),
                       target_id));
    if (!client->Attach()) {
      return false;
    }
    clients_[target_id] = std::move(client);
    return true;
  }

  void DetachTab(const std::string& target_id) override {
    clients_.erase(target_id);
  }

  void SendToTab(const std::string& target_id,
                 int command_id,
                 std::string json) override {
    if (auto it = clients_.find(target_id); it != clients_.end()) {
      it->second->Send(std::move(json),
                       base::BindOnce(
                           [](base::WeakPtr<Connection> connection, int id) {
                             return connection && connection->MayDispatch(id);
                           },
                           weak_factory_.GetWeakPtr(), command_id),
                       base::BindOnce(&Connection::OnDropped,
                                      weak_factory_.GetWeakPtr(), command_id));
    }
  }

  void CommandAnswered(const std::string& target_id) override {
    if (auto it = clients_.find(target_id); it != clients_.end()) {
      it->second->Answered();
    }
  }

  void OpenTab(const std::string& task,
               const std::string& saved_container,
               const GURL& url,
               OpenTabCallback done) override {
    AgentTabOpener* opener = GetAgentTabOpener();
    if (!opener) {
      std::move(done).Run(std::nullopt, "Koan.unavailable",
                          "agent tabs need the agent containers, which this "
                          "build does not have yet");
      return;
    }
    // A task's container is chosen when its first tab opens: a later tab
    // that asks for another one would silently land in the first.
    if (!forwarder_->NoteTask(client_id_, task, saved_container)) {
      std::move(done).Run(
          std::nullopt, "Koan.notGranted",
          "the task \"" + task +
              "\" is already open in another container; end it or use "
              "another task name");
      return;
    }
    // The containers open a saved container only if the user granted it to
    // this client (koan_container_grants.h); otherwise nothing opens.
    AgentTabRequest request;
    request.client_id = client_id_;
    request.task = task;
    request.saved_container = saved_container;
    request.url = url;
    opener->OpenAgentTab(request,
                         base::BindOnce(&AgentForwarder::Connection::OnOpened,
                                        weak_factory_.GetWeakPtr(), task,
                                        saved_container, std::move(done)));
  }

  void EndTask(const std::string& task) override {
    forwarder_->EndTask(client_id_, task);
  }

  bool CloseTab(const std::string& target_id) override {
    content::WebContents* contents = forwarder_->ContentsFor(target_id);
    if (!contents) {
      return false;
    }
    // WebContents::Close() goes to the tab's delegate directly: no beforeunload
    // dialog is shown for an agent's tab.
    contents->Close();
    return true;
  }

  void TakeOver(const std::optional<std::string>& target_id,
                OpenTabCallback done) override {
    forwarder_->RequestTakeOver(client_id_, target_id, std::move(done));
  }

  bool ReleaseUserTab(const std::string& target_id) override {
    return forwarder_->ReleaseUserTab(target_id);
  }

  bool IsInGrantedSite(const std::string& target_id, const GURL& url) override {
    return forwarder_->registry_.MayReachSite(target_id, SiteFor(url));
  }

  void AskToResume(const std::string& target_id) override {
    forwarder_->AskToResume(target_id);
  }

  base::DictValue BrowserVersion() override {
    base::DictValue version;
    version.Set("protocolVersion",
                content::DevToolsAgentHost::GetProtocolVersion());
    version.Set("product",
                "Koan/" + std::string(version_info::GetVersionNumber()));
    version.Set("revision", "");
    version.Set("userAgent", embedder_support::GetUserAgent());
    version.Set("jsVersion", "");
    return version;
  }

  void Audit(AuditRecord record) override {
    forwarder_->Audit(std::move(record));
  }

  void UserNeeded(const std::string& target_id,
                  std::string_view reason) override {
    // The interstitial is on screen in the tab: the user decides there. The
    // prompt in the indicator for page-initiated surfaces comes with them.
    AuditRecord record;
    record.time = base::Time::Now();
    record.event = "user-needed";
    record.client_id = client_id_;
    record.tab_id = target_id;
    record.summary = std::string(reason);
    record.outcome = AuditOutcome::kPaused;
    forwarder_->Audit(std::move(record));
  }

 private:
  bool MayDispatch(int command_id) { return facade_->MayDispatch(command_id); }
  void OnDropped(int command_id) { facade_->OnCommandDropped(command_id); }

  void OnTabMessage(const std::string& target_id, std::string_view json) {
    facade_->OnTabMessage(target_id, json);
  }

  void OnTabClosed(const std::string& target_id) {
    // Content detached the client (a navigation the agent may not follow, or
    // the host went away). The tab stays in the registry if it is open: the
    // agent can attach again once it is somewhere it may reach.
    facade_->OnTabDetached(target_id);
    clients_.erase(target_id);
  }

  void OnOpened(std::string task,
                std::string saved_container,
                OpenTabCallback done,
                content::WebContents* contents) {
    if (!contents && !saved_container.empty()) {
      // Never a silent throwaway tab instead (#219).
      forwarder_->ForgetTaskIfEmpty(client_id_, task);
      std::move(done).Run(
          std::nullopt, "Koan.notGranted",
          "the saved container \"" + saved_container +
              "\" was not opened: the user has not let this agent use it, "
              "it does not exist, or another task has it open (Settings > "
              "Agent Control)");
      return;
    }
    if (!contents) {
      // The containers open agent tabs only in a normal window with Spaces.
      std::move(done).Run(std::nullopt, "Koan.noWindow",
                          "no Koan window with Spaces is open to put the "
                          "agent's tab in; open one and try again");
      return;
    }
    const std::string target_id =
        forwarder_->AddAgentTab(contents, client_id_, task);
    if (target_id.empty()) {
      std::move(done).Run(std::nullopt, "Koan.unavailable",
                          "the tab could not be given to the agent");
      return;
    }
    std::move(done).Run(target_id, std::string(), std::string());
  }

  const raw_ptr<AgentForwarder> forwarder_;
  const std::string client_id_;
  std::map<std::string, std::unique_ptr<AgentDevToolsClient>, std::less<>>
      clients_;
  std::unique_ptr<CdpFacade> facade_;
  base::WeakPtrFactory<Connection> weak_factory_{this};
};

AgentForwarder::AgentForwarder(SendCallback send,
                               base::FilePath log_dir,
                               base::RepeatingClosure on_changed)
    : send_(std::move(send)),
      on_changed_(std::move(on_changed)),
      audit_(base::ThreadPool::CreateSequencedTaskRunner(
                 {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
                  base::TaskShutdownBehavior::BLOCK_SHUTDOWN}),
             std::move(log_dir),
             NewChainKey(),
             AuditLog::Limits()) {
  AgentLeaseManager::Get().SetOpenedTabHandler(base::BindRepeating(
      &AgentForwarder::OnOpenedFromAgentTab, weak_factory_.GetWeakPtr()));
  AgentPageSurfaces::Get().Install(
      base::BindRepeating(&AgentForwarder::OnPageSurface,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&AgentForwarder::NotifyChanged,
                          weak_factory_.GetWeakPtr()));
}

AgentForwarder::~AgentForwarder() {
  AgentLeaseManager::Get().SetOpenedTabHandler({});
  // The app is quitting: throwaway containers are in memory and go with it.
  Reset(/*end_tasks=*/false);
  AgentPageSurfaces::Get().Uninstall();
}

AgentForwarder::Connection* AgentForwarder::ConnectionFor(
    uint64_t connection_id,
    const std::string& client_id) {
  auto it = connections_.find(connection_id);
  if (it != connections_.end()) {
    // A connection speaks for one client for its whole life.
    return it->second->client_id() == client_id ? it->second.get() : nullptr;
  }
  if (client_id.empty()) {
    return nullptr;
  }
  auto connection =
      std::make_unique<Connection>(this, connection_id, client_id);
  Connection* raw = connection.get();
  connections_[connection_id] = std::move(connection);
  return raw;
}

void AgentForwarder::OnClientConnected(uint64_t connection_id,
                                       const std::string& client_id,
                                       const std::string& label) {
  labels_[client_id] = label;
  ConnectionFor(connection_id, client_id);
}

void AgentForwarder::OnClientMessage(uint64_t connection_id,
                                     AgentMessage message) {
  if (Connection* connection =
          ConnectionFor(connection_id, message.client_id)) {
    connection->facade().OnClientMessage(std::move(message));
  }
}

void AgentForwarder::OnClientDisconnected(uint64_t connection_id) {
  auto it = connections_.find(connection_id);
  if (it == connections_.end()) {
    return;
  }
  const std::string client_id = it->second->client_id();
  connections_.erase(it);
  // The client's last connection went: its tasks end, and their throwaway
  // containers are cleared (an agent that reconnects starts new ones).
  for (const auto& [id, connection] : connections_) {
    if (connection->client_id() == client_id) {
      return;
    }
  }
  EndClient(client_id);
}

void AgentForwarder::Reset(bool end_tasks) {
  for (auto& [id, request] : take_overs_) {
    CancelTakeOverDialog(request->dialog_id);
  }
  take_overs_.clear();
  connections_.clear();
  std::vector<std::string> targets;
  for (const auto& [target_id, watcher] : tabs_) {
    targets.push_back(target_id);
  }
  for (const std::string& target_id : targets) {
    RemoveTab(target_id, /*destroyed=*/false);
  }
  labels_.clear();
  std::map<std::string, std::map<std::string, std::string>, std::less<>> tasks =
      std::move(tasks_);
  tasks_.clear();
  if (end_tasks) {
    for (const auto& [client_id, names] : tasks) {
      for (const auto& [task, saved] : names) {
        containers::EndAgentTask(client_id, task);
      }
    }
  }
  resume_asked_.clear();
  NotifyChanged();
}

bool AgentForwarder::NoteTask(const std::string& client_id,
                              const std::string& task,
                              const std::string& saved_container) {
  auto [it, added] = tasks_[client_id].try_emplace(task, saved_container);
  return added || it->second == saved_container;
}

void AgentForwarder::ForgetTaskIfEmpty(const std::string& client_id,
                                       const std::string& task) {
  auto it = tasks_.find(client_id);
  if (it == tasks_.end()) {
    return;
  }
  for (const std::string& target_id : registry_.TargetsFor(client_id)) {
    const TargetEntry* entry = registry_.Find(target_id);
    if (entry && entry->agent_owned && entry->task == task) {
      return;
    }
  }
  it->second.erase(task);
  if (it->second.empty()) {
    tasks_.erase(it);
  }
}

void AgentForwarder::EndTask(const std::string& client_id,
                             const std::string& task) {
  // Out of the registry first (the facades say the tabs are gone), then the
  // containers close the tabs and clear the container.
  for (const std::string& target_id : registry_.TargetsFor(client_id)) {
    const TargetEntry* entry = registry_.Find(target_id);
    if (entry && entry->agent_owned && entry->task == task) {
      RemoveTab(target_id, /*destroyed=*/false);
    }
  }
  if (auto it = tasks_.find(client_id); it != tasks_.end()) {
    it->second.erase(task);
    if (it->second.empty()) {
      tasks_.erase(it);
    }
  }
  containers::EndAgentTask(client_id, task);
}

void AgentForwarder::EndClient(const std::string& client_id) {
  for (const std::string& target_id : registry_.TargetsFor(client_id)) {
    RemoveTab(target_id, /*destroyed=*/false);
  }
  auto it = tasks_.find(client_id);
  if (it == tasks_.end()) {
    return;
  }
  const std::map<std::string, std::string> names = std::move(it->second);
  tasks_.erase(it);
  for (const auto& [task, saved] : names) {
    containers::EndAgentTask(client_id, task);
  }
}

content::WebContents* AgentForwarder::ContentsFor(
    std::string_view target_id) const {
  auto it = tabs_.find(target_id);
  return it == tabs_.end() ? nullptr : it->second->web_contents();
}

std::string AgentForwarder::AddAgentTab(content::WebContents* contents,
                                        const std::string& client_id,
                                        const std::string& task) {
  // Never Incognito, Guest or a system profile (design section 13), whatever
  // opened it.
  Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  if (!profile || profile->IsOffTheRecord() || profile->IsSystemProfile() ||
      profile->IsGuestSession()) {
    return std::string();
  }
  std::string target_id = TargetIdFor(contents);
  if (target_id.empty() || registry_.AddAgentTab(target_id, client_id, task) !=
                               TargetRegistry::AddResult::kOk) {
    return std::string();
  }
  tabs_[target_id] = std::make_unique<TabWatcher>(this, contents, target_id);
  AgentLeaseManager::Get().Attach(contents, client_id, task,
                                  /*agent_owned=*/true);
  for (const auto& [id, connection] : connections_) {
    if (connection->client_id() == client_id) {
      connection->facade().OnTargetAdded(target_id);
    }
  }
  return target_id;
}

void AgentForwarder::RemoveTab(const std::string& target_id, bool destroyed) {
  const TargetEntry* entry = registry_.Find(target_id);
  const std::string client_id = entry ? entry->client_id : std::string();
  for (const auto& [id, connection] : connections_) {
    if (connection->client_id() == client_id) {
      connection->facade().OnTargetRemoved(target_id);
    }
  }
  registry_.Remove(target_id);
  auto it = tabs_.find(target_id);
  if (it == tabs_.end()) {
    return;
  }
  std::unique_ptr<TabWatcher> watcher = std::move(it->second);
  tabs_.erase(it);
  resume_asked_.erase(target_id);
  NotifyChanged();
  // A tab being destroyed ends its own lease.
  if (content::WebContents* contents = watcher->web_contents();
      contents && !destroyed) {
    AgentLeaseManager::Get().Detach(contents);
    // What a page held for the user is shown as Chrome would now.
    AgentPageSurfaces::Get().ReleaseTab(contents);
  }
}

void AgentForwarder::OnTabDestroyed(const std::string& target_id) {
  RemoveTab(target_id, /*destroyed=*/true);
}

void AgentForwarder::OnTabChanged(const std::string& target_id) {
  const TargetEntry* entry = registry_.Find(target_id);
  if (!entry) {
    return;
  }
  for (const auto& [id, connection] : connections_) {
    if (connection->client_id() == entry->client_id) {
      connection->facade().OnTargetChanged(target_id);
    }
  }
}

void AgentForwarder::OnOpenedFromAgentTab(content::WebContents* source,
                                          content::WebContents* opened) {
  const std::string source_id = TargetIdFor(source);
  const TargetEntry* entry = registry_.Find(source_id);
  if (!entry) {
    return;
  }
  // The same agent and task. Over the open-tab budgets it is an ordinary
  // background tab: it was opened, and closing what a page opened would be a
  // surprise, but the agent does not get it.
  AddAgentTab(opened, std::string(entry->client_id), std::string(entry->task));
}

void AgentForwarder::OnPageSurface(content::WebContents* contents,
                                   PageSurface surface,
                                   bool held) {
  const std::string target_id = TargetIdFor(contents);
  const TargetEntry* entry = registry_.Find(target_id);
  if (!entry) {
    return;
  }
  const std::string client_id = entry->client_id;
  if (held) {
    AuditRecord record;
    record.time = base::Time::Now();
    record.event = "user-needed";
    record.client_id = client_id;
    record.tab_id = target_id;
    record.summary = std::string(ReasonFor(surface));
    record.outcome = AuditOutcome::kPaused;
    Audit(std::move(record));
  }
  ForEachFacadeOf(client_id, [&](CdpFacade& facade) {
    facade.OnUserNeeded(target_id, ReasonFor(surface), held);
  });
}

void AgentForwarder::ForEachFacadeOf(std::string_view client_id,
                                     base::FunctionRef<void(CdpFacade&)> run) {
  for (const auto& [id, connection] : connections_) {
    if (connection->client_id() == client_id) {
      run(connection->facade());
    }
  }
}

void AgentForwarder::AuditEvent(std::string_view event,
                                const std::string& target_id,
                                const std::string& client_id) {
  AuditRecord record;
  record.time = base::Time::Now();
  record.event = std::string(event);
  record.client_id = client_id;
  record.tab_id = target_id;
  if (content::WebContents* contents = ContentsFor(target_id)) {
    const GURL& url = contents->GetLastCommittedURL();
    if (url.is_valid()) {
      record.tab_origin = url::Origin::Create(url).Serialize();
    }
  }
  Audit(std::move(record));
}

void AgentForwarder::RequestTakeOver(
    const std::string& client_id,
    const std::optional<std::string>& target_id,
    FacadeBackend::OpenTabCallback done) {
  content::WebContents* contents = nullptr;
  if (target_id) {
    if (scoped_refptr<content::DevToolsAgentHost> host =
            content::DevToolsAgentHost::GetForId(*target_id)) {
      contents = host->GetWebContents();
    }
  } else if (BrowserWindowInterface* browser =
                 GetLastActiveBrowserWindowInterfaceWithAnyProfile()) {
    // "The tab the user is looking at": the active tab of their last active
    // window (design section 9.3).
    if (tabs::TabInterface* tab = browser->GetActiveTabInterface()) {
      contents = tab->GetContents();
    }
  }
  // Only a tab in a window, of the original profile, on a page the agent may
  // reach at all, and on http(s): the grant is a site.
  tabs::TabInterface* tab =
      contents ? tabs::TabInterface::MaybeGetFromContents(contents) : nullptr;
  Profile* profile =
      contents ? Profile::FromBrowserContext(contents->GetBrowserContext())
               : nullptr;
  if (!tab || !profile || profile->IsOffTheRecord() ||
      profile->IsSystemProfile() || profile->IsGuestSession() ||
      !MayReachRenderFrameHost(contents->GetPrimaryMainFrame())) {
    std::move(done).Run(std::nullopt, "Koan.notPermitted",
                        "that tab cannot be given to an agent");
    return;
  }
  const std::string site = SiteFor(contents->GetLastCommittedURL());
  const std::string id = TargetIdFor(contents);
  if (site.empty() || id.empty()) {
    std::move(done).Run(std::nullopt, "Koan.notPermitted",
                        "only a tab showing a web page can be given to an "
                        "agent");
    return;
  }
  if (const TargetEntry* entry = registry_.Find(id)) {
    if (entry->client_id != client_id || entry->agent_owned) {
      std::move(done).Run(std::nullopt, "Koan.notPermitted",
                          "that tab is not the user's to give");
      return;
    }
    if (entry->state == TabState::kDriven) {
      std::move(done).Run(id, std::string(), std::string());
      return;
    }
  }
  if (take_overs_.contains(id)) {
    std::move(done).Run(std::nullopt, "Koan.busy",
                        "the user is already being asked about that tab");
    return;
  }
  const uint64_t request_id = next_take_over_id_++;
  auto owned = std::make_unique<TakeOverRequest>();
  TakeOverRequest& request = *owned;
  take_overs_[id] = std::move(owned);
  request.request_id = request_id;
  request.client_id = client_id;
  request.site = site;
  request.any_site =
      !containers::LimitsTakeOverToSite(contents->GetBrowserContext());
  request.contents = contents->GetWeakPtr();
  request.done = std::move(done);
  request.timer.Start(
      FROM_HERE, kTakeOverTimeout,
      base::BindOnce(&AgentForwarder::OnTakeOverAnswer,
                     weak_factory_.GetWeakPtr(), id, request_id, false));
  auto label = labels_.find(client_id);
  AuditEvent("take-over-asked", id, client_id);
  const uint64_t dialog_id = ShowTakeOverDialog(
      contents, label == labels_.end() ? std::string() : label->second, site,
      request.any_site,
      base::BindOnce(&AgentForwarder::OnTakeOverAnswer,
                     weak_factory_.GetWeakPtr(), id, request_id));
  // With no window to ask in, the answer (a no) has come already.
  if (auto it = take_overs_.find(id);
      it != take_overs_.end() && it->second->request_id == request_id) {
    it->second->dialog_id = dialog_id;
  }
}

void AgentForwarder::OnTakeOverAnswer(std::string target_id,
                                      uint64_t request_id,
                                      bool allowed) {
  auto it = take_overs_.find(target_id);
  if (it == take_overs_.end() || it->second->request_id != request_id) {
    return;
  }
  std::unique_ptr<TakeOverRequest> owned = std::move(it->second);
  take_overs_.erase(it);
  TakeOverRequest& request = *owned;
  request.timer.Stop();
  if (!allowed) {
    // The timeout: the dialog goes too.
    CancelTakeOverDialog(request.dialog_id);
  }
  content::WebContents* contents = request.contents.get();
  // The client must still be connected: a grant to an agent that went away
  // would outlive the task it was for.
  bool connected = false;
  for (const auto& [connection_id, connection] : connections_) {
    connected = connected || connection->client_id() == request.client_id;
  }
  allowed = allowed && connected;
  // The user approved the site they saw: if the tab left it meanwhile, no.
  if (allowed &&
      (!contents || SiteFor(contents->GetLastCommittedURL()) != request.site)) {
    allowed = false;
  }
  if (!allowed) {
    AuditEvent("take-over-denied", target_id, request.client_id);
    std::move(request.done)
        .Run(std::nullopt, "Koan.denied",
             "the user did not give the agent that tab");
    return;
  }
  if (registry_.GrantUserTab(target_id, request.client_id, TabState::kDriven,
                             request.site, request.any_site) !=
      TargetRegistry::AddResult::kOk) {
    std::move(request.done)
        .Run(std::nullopt, "Koan.notPermitted",
             "another agent controls that tab");
    return;
  }
  if (!tabs_.contains(target_id)) {
    tabs_[target_id] = std::make_unique<TabWatcher>(this, contents, target_id);
  }
  // A user tab driven in place: kept awake, its <select>s render in the page,
  // and it is not muted (AgentTabLease mutes only the agent's own tabs).
  AgentLeaseManager::Get().Attach(contents, request.client_id, std::string(),
                                  /*agent_owned=*/false);
  AuditEvent("take-over", target_id, request.client_id);
  ForEachFacadeOf(request.client_id,
                  [&](CdpFacade& facade) { facade.OnTargetAdded(target_id); });
  NotifyChanged();
  std::move(request.done).Run(target_id, std::string(), std::string());
}

bool AgentForwarder::ReleaseUserTab(const std::string& target_id) {
  const TargetEntry* entry = registry_.Find(target_id);
  if (!entry || entry->agent_owned) {
    return false;
  }
  AuditEvent("release", target_id, entry->client_id);
  RemoveTab(target_id, /*destroyed=*/false);
  return true;
}

void AgentForwarder::OnUserInput(const std::string& target_id) {
  const TargetEntry* entry = registry_.Find(target_id);
  if (!entry || entry->state != TabState::kDriven) {
    return;
  }
  // Detect after delivery: this input already reached the page; what the
  // agent queued behind it is not sent (CdpFacade::MayDispatch()).
  registry_.Pause(target_id);
  const std::string client_id = entry->client_id;
  AuditEvent("pause", target_id, client_id);
  ForEachFacadeOf(client_id, [&](CdpFacade& facade) {
    facade.OnTargetStateChanged(target_id, "Koan.paused", "user-input");
  });
  NotifyChanged();
}

void AgentForwarder::OnTabNavigated(const std::string& target_id,
                                    const GURL& url) {
  // With "Limit a taken-over tab to its site" on, the user allowed one site;
  // leaving it (a link, a redirect, the user typing in the omnibox) ends
  // write access. The agent may still read it.
  if (!registry_.LeftGrantedSite(target_id, SiteFor(url))) {
    return;
  }
  const std::string client_id = registry_.Find(target_id)->client_id;
  AuditEvent("site-left", target_id, client_id);
  ForEachFacadeOf(client_id, [&](CdpFacade& facade) {
    facade.OnTargetStateChanged(target_id, "Koan.siteLeft", "navigation");
  });
  NotifyChanged();
}

void AgentForwarder::AskToResume(const std::string& target_id) {
  resume_asked_.insert(target_id);
  NotifyChanged();
}

void AgentForwarder::HandBack(const std::string& target_id) {
  const TargetEntry* entry = registry_.Find(target_id);
  if (!entry || !registry_.Resume(target_id)) {
    return;
  }
  resume_asked_.erase(target_id);
  const std::string client_id = entry->client_id;
  AuditEvent("hand-back", target_id, client_id);
  ForEachFacadeOf(client_id, [&](CdpFacade& facade) {
    facade.OnTargetStateChanged(target_id, "Koan.resumed", "handed-back");
  });
  NotifyChanged();
}

void AgentForwarder::TakeBack(const std::string& target_id) {
  const TargetEntry* entry = registry_.Find(target_id);
  if (!entry) {
    return;
  }
  if (entry->agent_owned) {
    // The agent's own tab stays the agent's; taking it back from a pause is
    // keeping it paused, which it already is.
    return;
  }
  const std::string client_id = entry->client_id;
  ForEachFacadeOf(client_id, [&](CdpFacade& facade) {
    facade.OnTargetStateChanged(target_id, "Koan.released", "user");
  });
  AuditEvent("take-back", target_id, client_id);
  RemoveTab(target_id, /*destroyed=*/false);
}

std::vector<ControlledTab> AgentForwarder::ControlledTabs() const {
  std::vector<ControlledTab> tabs;
  for (const auto& [target_id, watcher] : tabs_) {
    const TargetEntry* entry = registry_.Find(target_id);
    if (!entry || (entry->agent_owned && entry->state != TabState::kPaused)) {
      continue;
    }
    ControlledTab tab;
    tab.target_id = target_id;
    auto label = labels_.find(entry->client_id);
    tab.label = label == labels_.end() ? std::string() : label->second;
    tab.state = entry->state;
    tab.agent_owned = entry->agent_owned;
    tab.resume_asked = resume_asked_.contains(target_id);
    tab.contents = watcher->web_contents();
    tabs.push_back(std::move(tab));
  }
  return tabs;
}

void AgentForwarder::NotifyChanged() {
  if (on_changed_) {
    on_changed_.Run();
  }
}

void AgentForwarder::Audit(AuditRecord record) {
  if (auto it = labels_.find(record.client_id); it != labels_.end()) {
    record.client_label = it->second;
  }
  audit_.AsyncCall(base::IgnoreResult(&AuditLog::Append))
      .WithArgs(std::move(record));
}

}  // namespace koan::agent
