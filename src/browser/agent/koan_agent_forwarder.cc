// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_forwarder.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/rand_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/time/default_tick_clock.h"
#include "chrome/browser/profiles/profile.h"
#include "components/embedder_support/user_agent_utils.h"
#include "components/security_interstitials/content/security_interstitial_tab_helper.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "koan/browser/agent/koan_agent_devtools_client.h"
#include "koan/browser/agent/koan_agent_facade.h"
#include "koan/browser/agent/koan_agent_lease.h"
#include "koan/browser/agent/koan_agent_tab_hooks.h"
#include "koan/browser/containers/koan_agent_containers.h"

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

}  // namespace

// Watches a tab in the registry: its end, and changes the facades announce.
class AgentForwarder::TabWatcher final : public content::WebContentsObserver {
 public:
  TabWatcher(AgentForwarder* forwarder,
             content::WebContents* contents,
             std::string target_id)
      : content::WebContentsObserver(contents),
        forwarder_(forwarder),
        target_id_(std::move(target_id)) {}

  // content::WebContentsObserver:
  void WebContentsDestroyed() override {
    // Destroys this.
    forwarder_->OnTabDestroyed(std::string(target_id_));
  }
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (handle->IsInPrimaryMainFrame() && handle->HasCommitted()) {
      forwarder_->OnTabChanged(target_id_);
    }
  }
  void TitleWasSetForMainFrame(content::RenderFrameHost*) override {
    forwarder_->OnTabChanged(target_id_);
  }

 private:
  const raw_ptr<AgentForwarder> forwarder_;
  const std::string target_id_;
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

  void SendToTab(const std::string& target_id, std::string json) override {
    if (auto it = clients_.find(target_id); it != clients_.end()) {
      it->second->Send(std::move(json));
    }
  }

  void CommandAnswered(const std::string& target_id) override {
    if (auto it = clients_.find(target_id); it != clients_.end()) {
      it->second->Answered();
    }
  }

  void OpenTab(const std::string& task,
               const GURL& url,
               OpenTabCallback done) override {
    AgentTabOpener* opener = GetAgentTabOpener();
    if (!opener) {
      std::move(done).Run(std::nullopt, "Koan.unavailable",
                          "agent tabs need the agent containers, which this "
                          "build does not have yet");
      return;
    }
    // Only the client, the task's name and the URL: the containers give every
    // task a throwaway container, and nothing the agent sends can name a saved
    // one (a saved container is a separate, user-approved grant).
    AgentTabRequest request;
    request.client_id = client_id_;
    request.task = task;
    request.url = url;
    forwarder_->NoteTask(client_id_, task);
    opener->OpenAgentTab(
        request,
        base::BindOnce(&AgentForwarder::Connection::OnOpened,
                       weak_factory_.GetWeakPtr(), task, std::move(done)));
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
                OpenTabCallback done,
                content::WebContents* contents) {
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

AgentForwarder::AgentForwarder(SendCallback send, base::FilePath log_dir)
    : send_(std::move(send)),
      audit_(base::ThreadPool::CreateSequencedTaskRunner(
                 {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
                  base::TaskShutdownBehavior::BLOCK_SHUTDOWN}),
             std::move(log_dir),
             NewChainKey(),
             AuditLog::Limits()) {
  AgentLeaseManager::Get().SetOpenedTabHandler(base::BindRepeating(
      &AgentForwarder::OnOpenedFromAgentTab, weak_factory_.GetWeakPtr()));
}

AgentForwarder::~AgentForwarder() {
  AgentLeaseManager::Get().SetOpenedTabHandler({});
  // The app is quitting: throwaway containers are in memory and go with it.
  Reset(/*end_tasks=*/false);
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
  connections_.clear();
  std::vector<std::string> targets;
  for (const auto& [target_id, watcher] : tabs_) {
    targets.push_back(target_id);
  }
  for (const std::string& target_id : targets) {
    RemoveTab(target_id, /*destroyed=*/false);
  }
  labels_.clear();
  std::map<std::string, std::set<std::string>, std::less<>> tasks =
      std::move(tasks_);
  tasks_.clear();
  if (end_tasks) {
    for (const auto& [client_id, names] : tasks) {
      for (const std::string& task : names) {
        containers::EndAgentTask(client_id, task);
      }
    }
  }
}

void AgentForwarder::NoteTask(const std::string& client_id,
                              const std::string& task) {
  tasks_[client_id].insert(task);
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
  const std::set<std::string> names = std::move(it->second);
  tasks_.erase(it);
  for (const std::string& task : names) {
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
  // A tab being destroyed ends its own lease.
  if (content::WebContents* contents = watcher->web_contents();
      contents && !destroyed) {
    AgentLeaseManager::Get().Detach(contents);
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

void AgentForwarder::Audit(AuditRecord record) {
  if (auto it = labels_.find(record.client_id); it != labels_.end()) {
    record.client_label = it->second;
  }
  audit_.AsyncCall(base::IgnoreResult(&AuditLog::Append))
      .WithArgs(std::move(record));
}

}  // namespace koan::agent
