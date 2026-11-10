// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_service.h"

#include <unistd.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/process/process_handle.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/default_clock.h"
#include "base/time/default_tick_clock.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/termination_notification.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "koan/browser/agent/koan_agent_auth.h"
#include "koan/browser/agent/koan_agent_forwarder.h"
#include "koan/browser/agent/koan_agent_gateway.h"
#include "koan/browser/agent/koan_agent_lease.h"
#include "koan/browser/agent/koan_agent_limits.h"
#include "koan/browser/agent/koan_agent_page_surfaces.h"
#include "koan/browser/agent/koan_agent_peer.h"
#include "koan/browser/agent/koan_agent_socket.h"
#include "koan/browser/koan_prefs.h"

namespace koan::agent {

namespace {

AgentControlService* g_service = nullptr;
bool g_terminated = false;

// The discovery file the CLI reads (design section 4.1): next to the socket's
// default place, 0600.
// The activity log (design section 6.3).
base::FilePath AuditLogDirectory(const base::FilePath& user_data_dir) {
  return user_data_dir.Append(FILE_PATH_LITERAL("AgentControl"))
      .Append(FILE_PATH_LITERAL("Log"));
}

base::FilePath DiscoveryFile(const base::FilePath& user_data_dir) {
  return user_data_dir.Append(FILE_PATH_LITERAL("AgentControl"))
      .Append(FILE_PATH_LITERAL("agent.json"));
}

void WriteDiscoveryFile(base::FilePath file, base::FilePath socket_path) {
  base::DictValue info;
  info.Set("socket", socket_path.AsUTF8Unsafe());
  info.Set("pid", static_cast<int>(base::GetCurrentProcId()));
  info.Set("version", version_info::GetVersionNumber());
  const std::optional<std::string> json = base::WriteJson(info);
  // The socket's directory is made 0700 by the gateway before it listens; the
  // fallback socket path (a long --user-data-dir) is elsewhere, so make sure.
  if (!json || !base::CreateDirectory(file.DirName())) {
    return;
  }
  const base::FilePath temp = file.AddExtensionASCII("tmp");
  if (!base::WriteFile(temp, *json) ||
      !base::SetPosixFilePermissions(temp, 0600) ||
      !base::ReplaceFile(temp, file, nullptr)) {
    base::DeleteFile(temp);
    LOG(WARNING) << "Koan agent control: could not write " << file;
  }
}

RateLimiter::Limits LimitsFrom(PrefService& local_state) {
  RateLimiter::Limits limits;
  limits.max_failures =
      ReadLimit(&local_state.GetValue(prefs::kAgentControlMaxAuthFailures),
                kFailuresLimit);
  limits.max_prompts_per_minute =
      ReadLimit(&local_state.GetValue(prefs::kAgentControlMaxPromptsPerMinute),
                kPromptsLimit);
  limits.block_duration = kBrowserBlockDuration;
  return limits;
}

}  // namespace

// Lives on the IO thread: owns the gateway and what it needs, and posts what
// the UI must know to the service.
class AgentControlService::GatewayHost final : public ApprovalPresenter,
                                               public GatewayObserver {
 public:
  GatewayHost(scoped_refptr<base::SequencedTaskRunner> ui,
              base::WeakPtr<AgentControlService> service)
      : ui_(std::move(ui)),
        service_(std::move(service)),
        resolver_(CreatePlatformPeerResolver()),
        store_(std::make_unique<InMemoryClientStore>()) {}

  ~GatewayHost() override { Stop(); }

  void Start(base::FilePath user_data_dir, RateLimiter::Limits limits) {
    if (gateway_) {
      gateway_->SetLimits(limits);
      return;
    }
    const uid_t uid = geteuid();
    const std::optional<base::FilePath> socket_path =
        ChooseSocketPath(user_data_dir, uid);
    if (!socket_path) {
      LOG(WARNING) << "Koan agent control: no socket path fits";
      return;
    }
    GatewayOptions options;
    options.socket_path = *socket_path;
    options.uid = uid;
    options.limits = limits;
    options.observer = this;
    gateway_ = std::make_unique<Gateway>(options, resolver_, store_.get(), this,
                                         base::DefaultTickClock::GetInstance(),
                                         base::DefaultClock::GetInstance());
    gateway_->Start(base::BindOnce(&GatewayHost::OnStarted,
                                   weak_factory_.GetWeakPtr(),
                                   DiscoveryFile(user_data_dir), *socket_path));
  }

  void Stop() {
    if (!gateway_) {
      return;
    }
    // Every connection's end is reported to the observer (this) while the
    // gateway goes.
    gateway_.reset();
    if (!discovery_file_.empty()) {
      file_runner_->PostTask(FROM_HERE,
                             base::GetDeleteFileCallback(discovery_file_));
      discovery_file_.clear();
    }
    PostToUi(&AgentControlService::OnListening, false);
  }

  // Closes everything and forgets every pairing. The store is replaced only
  // after the gateway that points to it is gone.
  void ForgetAll(base::FilePath user_data_dir,
                 bool restart,
                 RateLimiter::Limits limits) {
    Stop();
    store_ = std::make_unique<InMemoryClientStore>();
    if (restart) {
      Start(std::move(user_data_dir), limits);
    }
  }

  // ApprovalPresenter:
  void Show(const ApprovalRequest& request,
            base::OnceCallback<void(ApprovalDecision)> answer) override {
    PostToUi(&AgentControlService::ShowApproval, request, std::move(answer));
  }
  void Cancel(uint64_t request_id) override {
    PostToUi(&AgentControlService::CancelApproval, request_id);
  }

  // GatewayObserver:
  void OnClientConnected(uint64_t connection_id,
                         const std::string& client_id,
                         const std::string& label) override {
    PostToUi(&AgentControlService::OnClientConnected,
             ConnectedClient{connection_id, client_id, label});
  }
  void OnClientDisconnected(uint64_t connection_id) override {
    PostToUi(&AgentControlService::OnClientDisconnected, connection_id);
  }
  bool OnClientMessage(uint64_t connection_id, AgentMessage message) override {
    PostToUi(&AgentControlService::OnClientMessage, connection_id,
             std::move(message));
    return true;
  }

  // An answer or event for a connection, from the forwarder on the UI thread.
  void SendToClient(uint64_t connection_id, std::string json) {
    if (gateway_) {
      gateway_->SendToClient(connection_id, json);
    }
  }

 private:
  template <typename Method, typename... Args>
  void PostToUi(Method method, Args&&... args) {
    ui_->PostTask(FROM_HERE, base::BindOnce(method, service_,
                                            std::forward<Args>(args)...));
  }

  void OnStarted(base::FilePath discovery_file,
                 base::FilePath socket_path,
                 Gateway::StartResult result) {
    if (result != Gateway::StartResult::kOk) {
      LOG(WARNING) << "Koan agent control: the gateway did not start ("
                   << static_cast<int>(result) << ")";
      gateway_.reset();
      PostToUi(&AgentControlService::OnListening, false);
      return;
    }
    discovery_file_ = discovery_file;
    file_runner_->PostTask(FROM_HERE, base::BindOnce(&WriteDiscoveryFile,
                                                     std::move(discovery_file),
                                                     std::move(socket_path)));
    PostToUi(&AgentControlService::OnListening, true);
  }

  const scoped_refptr<base::SequencedTaskRunner> ui_;
  const base::WeakPtr<AgentControlService> service_;
  const std::shared_ptr<const PeerResolver> resolver_;
  // Pairings last until the browser quits: the Keychain-bound store of design
  // section 5.4 is a later change, and until it exists nothing is written.
  std::unique_ptr<InMemoryClientStore> store_;
  std::unique_ptr<Gateway> gateway_;
  base::FilePath discovery_file_;
  // Writing and deleting the discovery file run in order on one sequence, so a
  // quick off and on (or on and off) cannot leave the wrong file behind (#201
  // review).
  const scoped_refptr<base::SequencedTaskRunner> file_runner_ =
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN});
  base::WeakPtrFactory<GatewayHost> weak_factory_{this};
};

// static
AgentControlService* AgentControlService::GetOrCreate() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!g_service && !g_terminated && g_browser_process &&
      g_browser_process->local_state()) {
    // Never deleted: it stops everything when the app terminates, and the
    // process ends soon after.
    g_service = new AgentControlService(g_browser_process->local_state());
  }
  return g_service;
}

// static
AgentControlService* AgentControlService::Get() {
  return g_service;
}

AgentControlService::AgentControlService(PrefService* local_state)
    : local_state_(local_state) {
  // Here, not in the initialiser list: the weak pointer factory is the last
  // member.
  host_ = base::SequenceBound<GatewayHost>(
      content::GetIOThreadTaskRunner({}),
      base::SequencedTaskRunner::GetCurrentDefault(),
      weak_factory_.GetWeakPtr());
  base::FilePath user_data_dir;
  base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir);
  forwarder_ = std::make_unique<AgentForwarder>(
      base::BindRepeating(&AgentControlService::SendToClient,
                          weak_factory_.GetWeakPtr()),
      AuditLogDirectory(user_data_dir),
      base::BindRepeating(&AgentControlService::NotifyObservers,
                          base::Unretained(this)));
  registrar_.Init(local_state_);
  for (const char* pref :
       {prefs::kAgentControlEnabled, prefs::kAgentControlMaxPromptsPerMinute,
        prefs::kAgentControlMaxAuthFailures,
        ::prefs::kDevToolsRemoteDebuggingAllowed}) {
    registrar_.Add(pref, base::BindRepeating(&AgentControlService::Update,
                                             base::Unretained(this)));
  }
  terminating_ = browser_shutdown::AddAppTerminatingCallback(
      base::BindOnce(&AgentControlService::Shutdown, base::Unretained(this)));
  Update();
}

AgentControlService::~AgentControlService() = default;

void AgentControlService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void AgentControlService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void AgentControlService::Update() {
  // Enterprise policy RemoteDebuggingAllowed=false keeps it off (T15).
  const bool on =
      local_state_->GetBoolean(prefs::kAgentControlEnabled) &&
      local_state_->GetBoolean(::prefs::kDevToolsRemoteDebuggingAllowed);
  if (!on) {
    host_.AsyncCall(&GatewayHost::Stop);
    // Nothing is kept awake for an agent once Agent ready is off (T-ready-2).
    if (forwarder_) {
      forwarder_->Reset(/*end_tasks=*/true);
    }
    AgentLeaseManager::Get().DetachAll();
    return;
  }
  base::FilePath user_data_dir;
  if (!base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir)) {
    return;
  }
  host_.AsyncCall(&GatewayHost::Start)
      .WithArgs(user_data_dir, LimitsFrom(*local_state_));
}

void AgentControlService::StopAllAgents() {
  base::FilePath user_data_dir;
  base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir);
  const bool on =
      local_state_->GetBoolean(prefs::kAgentControlEnabled) &&
      local_state_->GetBoolean(::prefs::kDevToolsRemoteDebuggingAllowed);
  if (forwarder_) {
    forwarder_->Reset(/*end_tasks=*/true);
  }
  AgentLeaseManager::Get().DetachAll();
  host_.AsyncCall(&GatewayHost::ForgetAll)
      .WithArgs(user_data_dir, on && !user_data_dir.empty(),
                LimitsFrom(*local_state_));
}

std::vector<ControlledTab> AgentControlService::controlled_tabs() const {
  return forwarder_ ? forwarder_->ControlledTabs()
                    : std::vector<ControlledTab>();
}

void AgentControlService::HandBack(const std::string& target_id) {
  if (forwarder_) {
    forwarder_->HandBack(target_id);
  }
}

void AgentControlService::TakeBack(const std::string& target_id) {
  if (forwarder_) {
    forwarder_->TakeBack(target_id);
  }
}

std::vector<HeldPrompt> AgentControlService::user_prompts() const {
  return forwarder_ ? AgentPageSurfaces::Get().Prompts()
                    : std::vector<HeldPrompt>();
}

void AgentControlService::OpenPrompt(uint64_t id) {
  if (forwarder_) {
    AgentPageSurfaces::Get().Open(id);
  }
}

void AgentControlService::DismissPrompt(uint64_t id) {
  if (forwarder_) {
    AgentPageSurfaces::Get().Dismiss(id);
  }
}

void AgentControlService::Shutdown() {
  g_terminated = true;
  g_service = nullptr;
  registrar_.RemoveAll();
  forwarder_.reset();
  AgentLeaseManager::Get().DetachAll();
  // Destroys the host, and with it the gateway, on the IO thread; the socket
  // file goes with it.
  host_.Reset();
  clients_.clear();
  listening_ = false;
  NotifyObservers();
}

void AgentControlService::OnListening(bool listening) {
  listening_ = listening;
  if (!listening) {
    clients_.clear();
  }
  NotifyObservers();
}

void AgentControlService::OnClientConnected(ConnectedClient client) {
  if (forwarder_) {
    forwarder_->OnClientConnected(client.connection_id, client.client_id,
                                  client.label);
  }
  clients_.push_back(std::move(client));
  NotifyObservers();
}

void AgentControlService::OnClientMessage(uint64_t connection_id,
                                          AgentMessage message) {
  if (forwarder_) {
    forwarder_->OnClientMessage(connection_id, std::move(message));
  }
}

void AgentControlService::SendToClient(uint64_t connection_id,
                                       std::string json) {
  host_.AsyncCall(&GatewayHost::SendToClient)
      .WithArgs(connection_id, std::move(json));
}

void AgentControlService::OnClientDisconnected(uint64_t connection_id) {
  if (forwarder_) {
    forwarder_->OnClientDisconnected(connection_id);
  }
  std::erase_if(clients_, [&](const ConnectedClient& client) {
    return client.connection_id == connection_id;
  });
  NotifyObservers();
}

void AgentControlService::ShowApproval(
    ApprovalRequest request,
    base::OnceCallback<void(ApprovalDecision)> answer) {
  ShowApprovalDialog(request, std::move(answer));
}

void AgentControlService::CancelApproval(uint64_t request_id) {
  CancelApprovalDialog(request_id);
}

void AgentControlService::NotifyObservers() {
  for (Observer& observer : observers_) {
    observer.OnAgentControlChanged();
  }
}

}  // namespace koan::agent
