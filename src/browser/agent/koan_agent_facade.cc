// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_facade.h"

#include <limits>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/token.h"
#include "koan/browser/agent/koan_agent_contexts.h"
#include "koan/browser/agent/koan_agent_events.h"
#include "koan/browser/agent/koan_agent_filters.h"
#include "koan/browser/agent/koan_agent_policy.h"
#include "koan/browser/agent/koan_agent_reach.h"
#include "koan/browser/agent/koan_agent_redact.h"
#include "koan/browser/agent/koan_agent_registry.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_constants.h"

namespace koan::agent {

namespace {

constexpr int kServerError = -32000;

// Koan's own methods, answered here.
constexpr std::string_view kKoanPrefix = "Koan.";

bool IsTargetMethod(std::string_view method) {
  return method.starts_with("Target.");
}

std::string OriginOf(const GURL& url) {
  return url.is_valid() ? url::Origin::Create(url).Serialize() : std::string();
}

const char* StateName(TabState state) {
  switch (state) {
    case TabState::kReadable:
      return "readable";
    case TabState::kDriven:
      return "driven";
    case TabState::kPaused:
      return "paused";
  }
  return "";
}

}  // namespace

FacadeBackend::~FacadeBackend() = default;

// One attached tab: the session id the client uses for it, and what the
// filters keep for it.
struct CdpFacade::TabSession {
  std::string session_id;
  // The tab (the registry's target), for a child too: what the agent may do
  // in a child is what it may do in its tab.
  std::string target_id;
  ContextTracker contexts;
  StreamRegistry streams;
  // A child target (an out-of-process frame or a dedicated worker) that the
  // tab's auto-attach reported: Chromium's session id for it, the facade
  // session it was attached in, and its own target id and type. Empty for a
  // tab.
  std::string child_session;
  std::string parent_session_id;
  std::string child_target_id;
  std::string child_type;
  bool is_child() const { return !child_session.empty(); }
};

namespace {

// Children per tab the facade keeps (a page can make frames in a loop).
constexpr size_t kMaxChildSessions = 64;

// The child targets an agent may have: frames and the page's own workers.
// Shared and service workers outlive the tab and serve the user's other tabs
// of the site, so they are not the tab's to hand over.
bool IsChildType(std::string_view type) {
  return type == "iframe" || type == "worker";
}

}  // namespace

// A command sent to a tab and not answered yet.
struct CdpFacade::Pending {
  // The client's id for it; nullopt for the facade's own commands.
  std::optional<int> client_message_id;
  std::string session_id;
  std::string target_id;
  std::string method;
  std::string summary;
  // For IO.close: the stream it closes.
  std::string stream;
  base::TimeTicks start;
};

CdpFacade::CdpFacade(uint64_t connection_id,
                     std::string client_id,
                     TargetRegistry* registry,
                     FacadeBackend* backend,
                     const base::TickClock* tick_clock,
                     SendCallback send)
    : connection_id_(connection_id),
      client_id_(std::move(client_id)),
      registry_(registry),
      backend_(backend),
      tick_clock_(tick_clock),
      send_(std::move(send)) {}

CdpFacade::~CdpFacade() {
  // The backend's clients go with the connection: detach them, so that no
  // lease or command is left behind.
  for (const auto& [target_id, session_id] : session_by_target_) {
    backend_->DetachTab(target_id);
  }
}

bool CdpFacade::IsAttached(std::string_view target_id) const {
  return session_by_target_.contains(target_id);
}

CdpFacade::TabSession* CdpFacade::SessionFor(std::string_view session_id) {
  auto it = sessions_.find(session_id);
  return it == sessions_.end() ? nullptr : it->second.get();
}

CdpFacade::TabSession* CdpFacade::SessionForTarget(std::string_view target_id) {
  auto it = session_by_target_.find(target_id);
  return it == session_by_target_.end() ? nullptr : SessionFor(it->second);
}

bool CdpFacade::InScope(std::string_view target_id) const {
  const TargetEntry* entry = registry_->Find(target_id);
  return entry && entry->client_id == client_id_;
}

void CdpFacade::OnClientMessage(AgentMessage message) {
  // A message for the client's own browser-level methods, or for a tab through
  // the session the facade handed out. Koan.* and Target.* are the facade's
  // wherever they are sent.
  if (message.method.starts_with(kKoanPrefix)) {
    HandleKoanMethod(message);
    return;
  }
  TabSession* session = nullptr;
  if (message.session_id) {
    session = SessionFor(*message.session_id);
    if (!session) {
      ReplyError(message.id, message.session_id, "Koan.unknownSession",
                 "no session with this id");
      return;
    }
  }
  if (IsTargetMethod(message.method)) {
    HandleTargetMethod(message, session);
    return;
  }
  if (!session) {
    HandleBrowserMethod(message);
    return;
  }
  HandleSessionMethod(message, *session);
}

void CdpFacade::HandleBrowserMethod(AgentMessage& message) {
  const Decision decision = Classify(message.method);
  if (decision.verdict == Verdict::kNoOp) {
    AuditCommand(message.method, "", "", AuditOutcome::kOk, base::TimeDelta());
    Reply(message.id, message.session_id, base::DictValue());
    return;
  }
  if (message.method == "Browser.getVersion") {
    AuditCommand(message.method, "", "", AuditOutcome::kOk, base::TimeDelta());
    Reply(message.id, message.session_id, backend_->BrowserVersion());
    return;
  }
  // Everything else needs a tab: an agent has no browser target (section 4.2).
  AuditCommand(message.method, "", "", AuditOutcome::kDenied,
               base::TimeDelta());
  ReplyError(message.id, message.session_id, "Koan.notPermitted",
             message.method +
                 " is not available to agents here; attach to "
                 "a tab and send it with its sessionId");
}

void CdpFacade::HandleSessionMethod(AgentMessage& message,
                                    TabSession& session) {
  const std::string& method = message.method;
  const std::string summary = SummarizeCommand(method, message.params);
  const Decision decision = Classify(method);
  auto deny = [&](std::string_view code, std::string_view text) {
    AuditCommand(method, session.target_id, summary, AuditOutcome::kDenied,
                 base::TimeDelta());
    ReplyError(message.id, message.session_id, code, text);
  };
  switch (decision.verdict) {
    case Verdict::kDeny:
      deny("Koan.notPermitted", method + " is not allowed for agents");
      return;
    case Verdict::kGated:
      // v1 grants no gated capability (decision 11).
      deny("Koan.notPermitted",
           method + " needs a capability this client does not have");
      return;
    case Verdict::kNoOp:
      // Page.bringToFront and the like: agent tabs are never raised.
      AuditCommand(method, session.target_id, summary, AuditOutcome::kOk,
                   base::TimeDelta());
      Reply(message.id, message.session_id, base::DictValue());
      return;
    case Verdict::kFacade:
      if (method == "Browser.getVersion") {
        HandleBrowserMethod(message);
        return;
      }
      deny("Koan.notPermitted", method + " is not available in a tab session");
      return;
    case Verdict::kAllow:
      break;
  }

  // Design section 4.4: the agent never acts on a security interstitial (it
  // could press "Proceed"). Reads still work, so it can see why it stopped.
  if (!IsReadOnlyMethod(method)) {
    const std::optional<TabFacts> facts =
        backend_->DescribeTab(session.target_id);
    if (!facts || facts->interstitial) {
      deny("Koan.interstitial",
           "the tab shows a security warning; only the user can decide what "
           "to do");
      if (facts && interstitial_tabs_.insert(session.target_id).second) {
        base::DictValue params;
        params.Set("targetId", session.target_id);
        params.Set("reason", "interstitial");
        SendEvent("Koan.userNeeded", std::move(params), std::nullopt);
        backend_->UserNeeded(session.target_id, "interstitial");
      }
      return;
    }
    interstitial_tabs_.erase(session.target_id);
  }

  const TargetEntry* entry = registry_->Find(session.target_id);
  if (!registry_->MaySend(client_id_, session.target_id, method)) {
    if (entry && entry->client_id == client_id_ &&
        entry->state == TabState::kPaused) {
      AuditCommand(method, session.target_id, summary, AuditOutcome::kPaused,
                   base::TimeDelta());
      ReplyError(message.id, message.session_id, "Koan.paused",
                 "the user is using this tab; wait until they hand it back");
      return;
    }
    deny("Koan.notPermitted",
         entry && entry->client_id == client_id_
             ? method + " would change a tab this agent may only read"
             : "this tab is not in the agent's scope");
    return;
  }

  FilterContext context;
  context.tab_is_agent_owned = entry->agent_owned;
  context.streams = &session.streams;
  context.contexts = &session.contexts;
  if (std::optional<FilterError> error =
          ApplyParamFilter(decision.filter, message.params, context)) {
    deny(error->code, error->message);
    return;
  }

  // The user allowed one site in their tab (design section 9.2): the agent's
  // own navigation may not leave it. (A link that leaves it ends write
  // access when it commits.)
  if (!entry->agent_owned && method == "Page.navigate") {
    const std::string* url = message.params.FindString("url");
    if (!url || !backend_->IsInGrantedSite(session.target_id, GURL(*url))) {
      deny("Koan.offSite",
           "the user allowed this tab for its current site only; open other "
           "sites in the agent's own tabs");
      return;
    }
  }
  if (method == "Page.createIsolatedWorld") {
    session.contexts.OnIsolatedWorldRequested();
  }
  if (!Forward(message.id, session, method, summary,
               std::move(message.params))) {
    deny("Koan.invalidParams", "the parameters could not be encoded");
  }
}

bool CdpFacade::Forward(std::optional<int> client_message_id,
                        TabSession& session,
                        const std::string& method,
                        std::string summary,
                        base::DictValue params) {
  Pending pending;
  pending.client_message_id = client_message_id;
  pending.session_id = session.session_id;
  pending.target_id = session.target_id;
  pending.method = method;
  pending.summary = std::move(summary);
  pending.start = tick_clock_->NowTicks();
  if (method == "IO.close") {
    if (const std::string* handle = params.FindString("handle")) {
      pending.stream = *handle;
    }
  }
  const int id = next_command_id_;
  next_command_id_ = next_command_id_ == std::numeric_limits<int>::max()
                         ? 1
                         : next_command_id_ + 1;
  base::DictValue command;
  command.Set("id", id);
  command.Set("method", method);
  command.Set("params", std::move(params));
  // A child's command goes in Chromium's session for it, on the tab's client.
  if (session.is_child()) {
    command.Set("sessionId", session.child_session);
  }
  std::optional<std::string> json = base::WriteJson(command);
  if (!json) {
    return false;
  }
  pending_.emplace(id, std::move(pending));
  backend_->SendToTab(session.target_id, id, std::move(*json));
  return true;
}

void CdpFacade::HandleTargetMethod(AgentMessage& message, TabSession* session) {
  const std::string& method = message.method;
  const Decision decision = Classify(method);
  if (decision.verdict == Verdict::kNoOp) {
    // Target.activateTarget: never raised (T8, T-focus-2).
    AuditCommand(method, session ? session->target_id : "", "",
                 AuditOutcome::kOk, base::TimeDelta());
    Reply(message.id, message.session_id, base::DictValue());
    return;
  }
  if (decision.verdict != Verdict::kFacade) {
    AuditCommand(method, session ? session->target_id : "", "",
                 AuditOutcome::kDenied, base::TimeDelta());
    ReplyError(message.id, message.session_id, "Koan.notPermitted",
               method + " is not allowed for agents");
    return;
  }
  if (method == "Target.getTargets") {
    GetTargets(message);
  } else if (method == "Target.getTargetInfo") {
    GetTargetInfo(message, session);
  } else if (method == "Target.attachToTarget") {
    AttachToTarget(message);
  } else if (method == "Target.detachFromTarget") {
    DetachFromTarget(message, session);
  } else if (method == "Target.setAutoAttach") {
    SetAutoAttach(message, session);
    if (session) {
      return;  // forwarded, audited when it is answered
    }
  } else if (method == "Target.setDiscoverTargets") {
    SetDiscoverTargets(message);
  } else if (method == "Target.createTarget") {
    OpenTab(message, /*cdp_shape=*/true);
    return;  // audited when it is answered
  } else if (method == "Target.closeTarget") {
    CloseTarget(message);
    return;  // audited there
  } else {
    // A facade row with no handler: refuse rather than guess.
    AuditCommand(method, "", "", AuditOutcome::kDenied, base::TimeDelta());
    ReplyError(message.id, message.session_id, "Koan.unavailable",
               method + " is not implemented");
    return;
  }
  AuditCommand(method, session ? session->target_id : "", "", AuditOutcome::kOk,
               base::TimeDelta());
}

void CdpFacade::HandleKoanMethod(AgentMessage& message) {
  if (message.method == "Koan.listTabs") {
    ListTabs(message);
    AuditCommand(message.method, "", "", AuditOutcome::kOk, base::TimeDelta());
  } else if (message.method == "Koan.openTab") {
    OpenTab(message, /*cdp_shape=*/false);
  } else if (message.method == "Koan.closeTab") {
    CloseTarget(message);
  } else if (message.method == "Koan.endTask") {
    const std::string* task = message.params.FindString("task");
    const std::string name = task && !task->empty() ? *task : kDefaultTask;
    AuditCommand(message.method, "", "", AuditOutcome::kOk, base::TimeDelta());
    // Answer first: ending the task detaches its sessions.
    Reply(message.id, message.session_id, base::DictValue());
    backend_->EndTask(name);
  } else if (message.method == "Koan.takeOver") {
    TakeOver(message);
  } else if (message.method == "Koan.release") {
    Release(message);
  } else if (message.method == "Koan.resume") {
    Resume(message);
  } else {
    AuditCommand(message.method, "", "", AuditOutcome::kDenied,
                 base::TimeDelta());
    ReplyError(message.id, message.session_id, "Koan.unavailable",
               message.method + " is not in this build");
  }
}

std::optional<base::DictValue> CdpFacade::TargetInfo(
    const std::string& target_id) {
  if (!InScope(target_id)) {
    return std::nullopt;
  }
  std::optional<TabFacts> facts = backend_->DescribeTab(target_id);
  if (!facts) {
    return std::nullopt;
  }
  base::DictValue info;
  info.Set("targetId", target_id);
  info.Set("type", "page");
  info.Set("title", facts->title);
  info.Set("url", facts->url.possibly_invalid_spec());
  info.Set("attached", IsAttached(target_id));
  info.Set("canAccessOpener", false);
  return info;
}

void CdpFacade::GetTargets(const AgentMessage& message) {
  base::ListValue infos;
  for (const std::string& target_id : registry_->TargetsFor(client_id_)) {
    if (std::optional<base::DictValue> info = TargetInfo(target_id)) {
      infos.Append(std::move(*info));
    }
  }
  base::DictValue result;
  result.Set("targetInfos", std::move(infos));
  Reply(message.id, message.session_id, std::move(result));
}

void CdpFacade::GetTargetInfo(const AgentMessage& message,
                              TabSession* session) {
  const std::string* target_id = message.params.FindString("targetId");
  std::string id = target_id ? *target_id
                   : session ? session->target_id
                             : std::string();
  std::optional<base::DictValue> info = TargetInfo(id);
  if (!info) {
    ReplyError(message.id, message.session_id, "Koan.unknownTarget",
               "no target with this id");
    return;
  }
  base::DictValue result;
  result.Set("targetInfo", std::move(*info));
  Reply(message.id, message.session_id, std::move(result));
}

void CdpFacade::AttachToTarget(const AgentMessage& message) {
  const std::string* target_id = message.params.FindString("targetId");
  if (!target_id || !InScope(*target_id)) {
    ReplyError(message.id, message.session_id, "Koan.unknownTarget",
               "no target with this id");
    return;
  }
  // Only flat sessions: the facade never relays Target.sendMessageToTarget.
  if (message.params.FindBool("flatten") != true) {
    ReplyError(message.id, message.session_id, "Koan.invalidParams",
               "only flatten: true is supported");
    return;
  }
  TabSession* session = Attach(*target_id);
  if (!session) {
    ReplyError(message.id, message.session_id, "Koan.unavailable",
               "the tab could not be attached");
    return;
  }
  base::DictValue result;
  result.Set("sessionId", session->session_id);
  Reply(message.id, message.session_id, std::move(result));
}

CdpFacade::TabSession* CdpFacade::Attach(const std::string& target_id) {
  // One session per tab and connection: a second attach is given the same
  // session (the facade cannot tell two of the client's users apart).
  if (TabSession* existing = SessionForTarget(target_id)) {
    return existing;
  }
  std::optional<base::DictValue> info = TargetInfo(target_id);
  if (!info || !backend_->AttachTab(target_id)) {
    return nullptr;
  }
  auto session = std::make_unique<TabSession>();
  session->session_id = base::Token::CreateRandom().ToString();
  session->target_id = target_id;
  TabSession* raw = session.get();
  session_by_target_[target_id] = session->session_id;
  sessions_[session->session_id] = std::move(session);

  info->Set("attached", true);
  base::DictValue params;
  params.Set("sessionId", raw->session_id);
  params.Set("targetInfo", std::move(*info));
  params.Set("waitingForDebugger", false);
  SendEvent("Target.attachedToTarget", std::move(params), std::nullopt);
  return raw;
}

void CdpFacade::DetachFromTarget(const AgentMessage& message,
                                 TabSession* session) {
  std::string session_id;
  if (const std::string* id = message.params.FindString("sessionId")) {
    session_id = *id;
  } else if (const std::string* target_id =
                 message.params.FindString("targetId")) {
    auto it = session_by_target_.find(*target_id);
    if (it != session_by_target_.end()) {
      session_id = it->second;
    }
  } else if (session) {
    session_id = session->session_id;
  }
  TabSession* ending = SessionFor(session_id);
  if (!ending) {
    ReplyError(message.id, message.session_id, "Koan.unknownSession",
               "no session with this id");
    return;
  }
  // Answer first: the reply may travel in the session being ended.
  Reply(message.id, message.session_id, base::DictValue());
  if (ending->is_child()) {
    // Chromium detaches it in the session it was attached in.
    if (TabSession* parent = SessionFor(ending->parent_session_id)) {
      base::DictValue params;
      params.Set("sessionId", ending->child_session);
      SendInternal(*parent, "Target.detachFromTarget", std::move(params));
    }
  }
  EndSession(session_id, /*tell_client=*/true);
}

void CdpFacade::SetAutoAttach(const AgentMessage& message,
                              TabSession* session) {
  const bool auto_attach =
      message.params.FindBool("autoAttach").value_or(false);
  if (auto_attach && message.params.FindBool("flatten") != true) {
    ReplyError(message.id, message.session_id, "Koan.invalidParams",
               "only flatten: true is supported");
    return;
  }
  if (session) {
    // Inside a tab (or a child of it): the tab's own auto-attach reports its
    // out-of-process frames and workers, each checked against the agent's
    // reach by content (the client's MayAttachToRenderFrameHost and
    // MayAttachToURL) and by OnChildAttached(). No filter: the facade decides
    // which children the agent gets.
    base::DictValue params;
    params.Set("autoAttach", auto_attach);
    params.Set(
        "waitForDebuggerOnStart",
        message.params.FindBool("waitForDebuggerOnStart").value_or(false));
    params.Set("flatten", true);
    if (!Forward(message.id, *session, message.method, std::string(),
                 std::move(params))) {
      ReplyError(message.id, message.session_id, "Koan.invalidParams",
                 "the parameters could not be encoded");
    }
    return;
  }
  auto_attach_ = auto_attach;
  // A tab is never paused at start (waitForDebuggerOnStart is accepted and
  // has nothing to wait for: the agent's tabs open through Koan).
  Reply(message.id, message.session_id, base::DictValue());
  if (auto_attach_) {
    for (const std::string& target_id : registry_->TargetsFor(client_id_)) {
      Attach(target_id);
    }
  }
}

void CdpFacade::SetDiscoverTargets(const AgentMessage& message) {
  const bool discover = message.params.FindBool("discover").value_or(false);
  Reply(message.id, message.session_id, base::DictValue());
  const bool was = discover_targets_;
  discover_targets_ = discover;
  if (discover && !was) {
    for (const std::string& target_id : registry_->TargetsFor(client_id_)) {
      if (std::optional<base::DictValue> info = TargetInfo(target_id)) {
        base::DictValue params;
        params.Set("targetInfo", std::move(*info));
        SendEvent("Target.targetCreated", std::move(params), std::nullopt);
      }
    }
  }
}

void CdpFacade::CloseTarget(const AgentMessage& message) {
  const std::string* target_id = message.params.FindString("targetId");
  const TargetEntry* entry = target_id ? registry_->Find(*target_id) : nullptr;
  if (!entry || entry->client_id != client_id_) {
    AuditCommand(message.method, "", "", AuditOutcome::kDenied,
                 base::TimeDelta());
    ReplyError(message.id, message.session_id, "Koan.unknownTarget",
               "no target with this id");
    return;
  }
  // The user's own tabs are handed back, never closed by an agent.
  if (!entry->agent_owned) {
    AuditCommand(message.method, *target_id, "", AuditOutcome::kDenied,
                 base::TimeDelta());
    ReplyError(message.id, message.session_id, "Koan.notPermitted",
               "an agent may close only the tabs it opened");
    return;
  }
  const std::string id = *target_id;
  const bool closed = backend_->CloseTab(id);
  AuditCommand(message.method, id, "",
               closed ? AuditOutcome::kOk : AuditOutcome::kError,
               base::TimeDelta());
  base::DictValue result;
  result.Set("success", closed);
  Reply(message.id, message.session_id, std::move(result));
}

void CdpFacade::OpenTab(const AgentMessage& message, bool cdp_shape) {
  const std::string summary = SummarizeCommand(message.method, message.params);
  auto deny = [&](std::string_view code, std::string_view text) {
    AuditCommand(message.method, "", summary, AuditOutcome::kDenied,
                 base::TimeDelta());
    ReplyError(message.id, message.session_id, code, text);
  };
  base::DictValue params = message.params.Clone();
  // A new tab with no URL is about:blank, as in CDP.
  if (const std::string* url = params.FindString("url"); !url || url->empty()) {
    params.Set("url", url::kAboutBlankURL);
  }
  if (std::optional<FilterError> error = ApplyParamFilter(
          ParamFilter::kNavigationUrl, params, FilterContext())) {
    deny(error->code, error->message);
    return;
  }
  // Never a new window or browser context, and never a foreground tab: the
  // agent's tab opens in the background whatever it asks (T-focus-1).
  if (cdp_shape && (params.FindString("browserContextId") ||
                    params.FindBool("newWindow").value_or(false))) {
    deny("Koan.notPermitted",
         "agents open tabs in their own space; use Target.createTarget "
         "without newWindow or browserContextId");
    return;
  }
  std::string task = kDefaultTask;
  if (const std::string* named = message.params.FindString("task");
      !cdp_shape && named && !named->empty()) {
    if (named->size() > 64 || !base::IsStringUTF8(*named)) {
      deny("Koan.invalidParams", "task must be 1 to 64 characters");
      return;
    }
    task = *named;
  }
  switch (registry_->CanAddAgentTab(client_id_, task)) {
    case TargetRegistry::AddResult::kOk:
      break;
    case TargetRegistry::AddResult::kTaskFull:
      deny("Koan.tooManyTabs", "this task has as many tabs as it may open");
      return;
    case TargetRegistry::AddResult::kClientFull:
      deny("Koan.tooManyTabs", "this agent has as many tabs as it may open");
      return;
    case TargetRegistry::AddResult::kBrowserFull:
      deny("Koan.tooManyTabs", "agents have as many tabs as they may open");
      return;
    case TargetRegistry::AddResult::kExists:
    case TargetRegistry::AddResult::kInvalid:
      deny("Koan.invalidParams", "the task name is not valid");
      return;
  }
  const GURL url(*params.FindString("url"));
  backend_->OpenTab(
      task, url,
      base::BindOnce(&CdpFacade::OnTabOpened, weak_factory_.GetWeakPtr(),
                     message.id, message.method, message.session_id,
                     tick_clock_->NowTicks(), summary));
}

void CdpFacade::OnTabOpened(int client_message_id,
                            std::string method,
                            std::optional<std::string> session_id,
                            base::TimeTicks start,
                            std::string summary,
                            std::optional<std::string> target_id,
                            std::string error_code,
                            std::string error_message) {
  const base::TimeDelta duration = tick_clock_->NowTicks() - start;
  if (!target_id) {
    AuditCommand(method, "", std::move(summary), AuditOutcome::kError,
                 duration);
    ReplyError(client_message_id, session_id, error_code, error_message);
    return;
  }
  AuditCommand(method, *target_id, std::move(summary), AuditOutcome::kOk,
               duration);
  base::DictValue result;
  result.Set("targetId", *target_id);
  Reply(client_message_id, session_id, std::move(result));
  // The forwarder has told every facade of the client (OnTargetAdded()), which
  // announced and, with auto-attach, attached it.
}

void CdpFacade::ListTabs(const AgentMessage& message) {
  base::ListValue tabs;
  for (const std::string& target_id : registry_->TargetsFor(client_id_)) {
    const TargetEntry* entry = registry_->Find(target_id);
    std::optional<TabFacts> facts = backend_->DescribeTab(target_id);
    if (!entry || !facts) {
      continue;
    }
    base::DictValue tab;
    tab.Set("targetId", target_id);
    tab.Set("title", facts->title);
    tab.Set("url", facts->url.possibly_invalid_spec());
    tab.Set("task", entry->task);
    tab.Set("agentOwned", entry->agent_owned);
    tab.Set("state", StateName(entry->state));
    tab.Set("attached", IsAttached(target_id));
    tabs.Append(std::move(tab));
  }
  base::DictValue result;
  result.Set("tabs", std::move(tabs));
  Reply(message.id, message.session_id, std::move(result));
}

void CdpFacade::TakeOver(const AgentMessage& message) {
  // Each tab asks the user again; the capability is only the right to ask.
  if (!message.capabilities.drive) {
    AuditCommand(message.method, "", "", AuditOutcome::kDenied,
                 base::TimeDelta());
    ReplyError(message.id, message.session_id, "Koan.notPermitted",
               "this agent may not ask to control your tabs (capability "
               "drive)");
    return;
  }
  std::optional<std::string> target_id;
  if (const std::string* given = message.params.FindString("targetId")) {
    target_id = *given;
  }
  backend_->TakeOver(
      target_id,
      base::BindOnce(&CdpFacade::OnTabOpened, weak_factory_.GetWeakPtr(),
                     message.id, message.method, message.session_id,
                     tick_clock_->NowTicks(), std::string()));
}

void CdpFacade::Release(const AgentMessage& message) {
  const std::string* target_id = message.params.FindString("targetId");
  const TargetEntry* entry = target_id ? registry_->Find(*target_id) : nullptr;
  if (!entry || entry->client_id != client_id_ || entry->agent_owned) {
    AuditCommand(message.method, "", "", AuditOutcome::kDenied,
                 base::TimeDelta());
    ReplyError(message.id, message.session_id, "Koan.unknownTarget",
               "no user tab with this id; close the agent's own tabs with "
               "Koan.closeTab");
    return;
  }
  const std::string id = *target_id;
  AuditCommand("release", id, "", AuditOutcome::kOk, base::TimeDelta());
  // Answer first: releasing ends the tab's session.
  Reply(message.id, message.session_id, base::DictValue());
  backend_->ReleaseUserTab(id);
}

void CdpFacade::Resume(const AgentMessage& message) {
  const std::string* target_id = message.params.FindString("targetId");
  const TargetEntry* entry = target_id ? registry_->Find(*target_id) : nullptr;
  if (!entry || entry->client_id != client_id_) {
    ReplyError(message.id, message.session_id, "Koan.unknownTarget",
               "no target with this id");
    return;
  }
  if (entry->state != TabState::kPaused) {
    Reply(message.id, message.session_id,
          base::DictValue().Set("state", StateName(entry->state)));
    return;
  }
  // Only the user resumes (design section 9.2): this asks them.
  AuditCommand(message.method, *target_id, "", AuditOutcome::kPaused,
               base::TimeDelta());
  backend_->AskToResume(*target_id);
  Reply(message.id, message.session_id,
        base::DictValue().Set("state", "paused").Set("asked", true));
}

void CdpFacade::OnTargetStateChanged(const std::string& target_id,
                                     std::string_view event,
                                     std::string_view reason) {
  const TargetEntry* entry = registry_->Find(target_id);
  if (!entry || entry->client_id != client_id_) {
    return;
  }
  base::DictValue params;
  params.Set("targetId", target_id);
  params.Set("state", StateName(entry->state));
  params.Set("reason", reason);
  SendEvent(event, std::move(params), std::nullopt);
}

void CdpFacade::OnUserNeeded(const std::string& target_id,
                             std::string_view reason,
                             bool needed) {
  if (!InScope(target_id)) {
    return;
  }
  base::DictValue params;
  params.Set("targetId", target_id);
  params.Set("reason", reason);
  SendEvent(needed ? "Koan.userNeeded" : "Koan.userNeededDone",
            std::move(params), std::nullopt);
}

bool CdpFacade::MayDispatch(int command_id) const {
  auto it = pending_.find(command_id);
  if (it == pending_.end()) {
    return false;
  }
  // The facade's own commands (IO.close) always run.
  if (!it->second.client_message_id) {
    return true;
  }
  return registry_->MaySend(client_id_, it->second.target_id,
                            it->second.method) &&
         !BehindInterstitial(it->second.target_id, it->second.method);
}

bool CdpFacade::BehindInterstitial(const std::string& target_id,
                                   std::string_view method) const {
  if (IsReadOnlyMethod(method)) {
    return false;
  }
  const std::optional<TabFacts> facts = backend_->DescribeTab(target_id);
  return !facts || facts->interstitial;
}

void CdpFacade::OnCommandDropped(int command_id) {
  auto it = pending_.find(command_id);
  if (it == pending_.end()) {
    return;
  }
  Pending pending = std::move(it->second);
  pending_.erase(it);
  if (!pending.client_message_id) {
    return;
  }
  if (pending.method == "Page.createIsolatedWorld") {
    if (TabSession* session = SessionFor(pending.session_id)) {
      session->contexts.OnIsolatedWorldAnswered(std::nullopt);
    }
  }
  // An interstitial came up while it waited, or the user's input paused the
  // tab.
  const bool interstitial =
      BehindInterstitial(pending.target_id, pending.method);
  AuditCommand(pending.method, pending.target_id, pending.summary,
               interstitial ? AuditOutcome::kDenied : AuditOutcome::kPaused,
               tick_clock_->NowTicks() - pending.start);
  if (interstitial) {
    ReplyError(*pending.client_message_id, pending.session_id,
               "Koan.interstitial",
               "the tab shows a security warning; only the user can decide "
               "what to do");
    return;
  }
  ReplyError(*pending.client_message_id, pending.session_id, "Koan.paused",
             "the user is using this tab; wait until they hand it back");
}

bool CdpFacade::Redacts(const std::string& target_id) const {
  const TargetEntry* entry = registry_->Find(target_id);
  return !entry || (!entry->agent_owned && entry->state != TabState::kDriven);
}

void CdpFacade::EndSession(const std::string& session_id, bool tell_client) {
  // Its children first: they go with it.
  std::vector<std::string> children;
  for (const auto& [id, child] : sessions_) {
    if (child->parent_session_id == session_id) {
      children.push_back(id);
    }
  }
  for (const std::string& child : children) {
    EndSession(child, tell_client);
  }
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    return;
  }
  std::unique_ptr<TabSession> session = std::move(it->second);
  sessions_.erase(it);
  if (session->is_child()) {
    child_by_chromium_session_.erase(session->child_session);
  } else {
    session_by_target_.erase(session->target_id);
  }
  // Commands in flight are answered: the tab will not.
  for (auto pending = pending_.begin(); pending != pending_.end();) {
    if (pending->second.session_id != session_id) {
      ++pending;
      continue;
    }
    if (pending->second.client_message_id) {
      AuditCommand(pending->second.method, pending->second.target_id,
                   pending->second.summary, AuditOutcome::kError,
                   tick_clock_->NowTicks() - pending->second.start);
      ReplyError(*pending->second.client_message_id, session_id,
                 "Koan.targetClosed",
                 session->is_child() ? "the frame or worker went away"
                                     : "the tab went away");
    }
    // A child's tab stays attached: what it held for the command goes now
    // (a tab's is released by DetachTab()).
    if (session->is_child()) {
      backend_->CommandAnswered(session->target_id);
    }
    pending = pending_.erase(pending);
  }
  if (session->is_child()) {
    if (tell_client) {
      base::DictValue params;
      params.Set("sessionId", session_id);
      params.Set("targetId", session->child_target_id);
      SendEvent("Target.detachedFromTarget", std::move(params),
                session->parent_session_id);
    }
    return;
  }
  backend_->DetachTab(session->target_id);
  if (tell_client) {
    base::DictValue params;
    params.Set("sessionId", session_id);
    params.Set("targetId", session->target_id);
    SendEvent("Target.detachedFromTarget", std::move(params), std::nullopt);
  }
}

void CdpFacade::OnTabDetached(const std::string& target_id) {
  if (TabSession* session = SessionForTarget(target_id)) {
    EndSession(std::string(session->session_id), /*tell_client=*/true);
  }
}

void CdpFacade::OnTargetAdded(const std::string& target_id) {
  if (!InScope(target_id)) {
    return;
  }
  if (discover_targets_) {
    if (std::optional<base::DictValue> info = TargetInfo(target_id)) {
      base::DictValue params;
      params.Set("targetInfo", std::move(*info));
      SendEvent("Target.targetCreated", std::move(params), std::nullopt);
    }
  }
  if (auto_attach_) {
    Attach(target_id);
  }
}

void CdpFacade::OnTargetRemoved(const std::string& target_id) {
  OnTabDetached(target_id);
  if (discover_targets_) {
    base::DictValue params;
    params.Set("targetId", target_id);
    SendEvent("Target.targetDestroyed", std::move(params), std::nullopt);
  }
}

void CdpFacade::OnTargetChanged(const std::string& target_id) {
  if (!discover_targets_) {
    return;
  }
  if (std::optional<base::DictValue> info = TargetInfo(target_id)) {
    base::DictValue params;
    params.Set("targetInfo", std::move(*info));
    SendEvent("Target.targetInfoChanged", std::move(params), std::nullopt);
  }
}

void CdpFacade::OnTabMessage(const std::string& target_id,
                             std::string_view json) {
  TabSession* session = SessionForTarget(target_id);
  if (!session) {
    return;
  }
  std::optional<base::DictValue> message =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!message) {
    return;
  }
  // A child's message: only one the facade accepted (OnChildAttached()), and
  // only of this tab.
  if (const std::string* child = message->FindString("sessionId")) {
    auto it = child_by_chromium_session_.find(*child);
    TabSession* child_session = it == child_by_chromium_session_.end()
                                    ? nullptr
                                    : SessionFor(it->second);
    if (!child_session || child_session->target_id != target_id) {
      return;
    }
    session = child_session;
  }
  if (message->contains("id")) {
    HandleTabAnswer(*session, std::move(*message));
  } else if (!HandleChildTargetEvent(*session, *message)) {
    HandleTabEvent(*session, std::move(*message));
  }
}

bool CdpFacade::HandleChildTargetEvent(TabSession& session,
                                       const base::DictValue& event) {
  const std::string* method = event.FindString("method");
  const base::DictValue* params = event.FindDict("params");
  if (!method || !method->starts_with("Target.") || !params) {
    return false;
  }
  if (*method == "Target.attachedToTarget") {
    OnChildAttached(session, *params);
  } else if (*method == "Target.detachedFromTarget") {
    const std::string* child = params->FindString("sessionId");
    auto it = child ? child_by_chromium_session_.find(*child)
                    : child_by_chromium_session_.end();
    if (it != child_by_chromium_session_.end()) {
      EndSession(std::string(it->second), /*tell_client=*/true);
    }
  } else if (*method == "Target.targetInfoChanged") {
    const base::DictValue* info = params->FindDict("targetInfo");
    const std::string* target = info ? info->FindString("targetId") : nullptr;
    for (const auto& [id, child] : sessions_) {
      if (target && child->is_child() && child->child_target_id == *target &&
          child->parent_session_id == session.session_id) {
        base::DictValue changed;
        changed.Set("targetInfo", ChildTargetInfo(*info));
        SendEvent("Target.targetInfoChanged", std::move(changed),
                  session.session_id);
        break;
      }
    }
  }
  // Every other Target.* event of a tab (targetCreated, ...) is not the
  // agent's: the facade's own Target.* describe its scope.
  return true;
}

void CdpFacade::OnChildAttached(TabSession& parent,
                                const base::DictValue& params) {
  const std::string* child = params.FindString("sessionId");
  const base::DictValue* info = params.FindDict("targetInfo");
  if (!child || child->empty() || !info) {
    return;
  }
  const std::string* type = info->FindString("type");
  const std::string* url = info->FindString("url");
  const std::string* child_target = info->FindString("targetId");
  size_t children = 0;
  for (const auto& [id, session] : sessions_) {
    children += session->is_child() && session->target_id == parent.target_id;
  }
  // Content let the client attach (it asked MayAttachToRenderFrameHost or
  // MayAttachToURL); the facade checks the child again by what it is, and a
  // refused one is detached (which also lets one waiting for the debugger
  // run).
  if (!type || !IsChildType(*type) || !url ||
      !IsReachableFrameUrl(GURL(*url)) || !child_target ||
      children >= kMaxChildSessions ||
      child_by_chromium_session_.contains(*child)) {
    base::DictValue detach;
    detach.Set("sessionId", *child);
    SendInternal(parent, "Target.detachFromTarget", std::move(detach));
    return;
  }
  auto session = std::make_unique<TabSession>();
  session->session_id = base::Token::CreateRandom().ToString();
  session->target_id = parent.target_id;
  session->child_session = *child;
  session->parent_session_id = parent.session_id;
  session->child_target_id = *child_target;
  session->child_type = *type;
  const std::string id = session->session_id;
  child_by_chromium_session_[*child] = id;
  sessions_[id] = std::move(session);

  base::DictValue attached;
  attached.Set("sessionId", id);
  attached.Set("targetInfo", ChildTargetInfo(*info));
  attached.Set("waitingForDebugger",
               params.FindBool("waitingForDebugger").value_or(false));
  SendEvent("Target.attachedToTarget", std::move(attached), parent.session_id);
}

// static
base::DictValue CdpFacade::ChildTargetInfo(const base::DictValue& info) {
  // Only what describes the child: never the browser context or the opener.
  base::DictValue copy;
  for (std::string_view key : {"targetId", "type", "title", "url"}) {
    if (const std::string* value = info.FindString(key)) {
      copy.Set(key, *value);
    }
  }
  copy.Set("attached", true);
  copy.Set("canAccessOpener", false);
  return copy;
}

void CdpFacade::HandleTabAnswer(TabSession& session, base::DictValue answer) {
  const std::optional<int> id = answer.FindInt("id");
  if (!id) {
    return;
  }
  auto it = pending_.find(*id);
  if (it == pending_.end() || it->second.session_id != session.session_id) {
    return;
  }
  Pending pending = std::move(it->second);
  pending_.erase(it);
  backend_->CommandAnswered(session.target_id);

  base::DictValue* result = answer.FindDict("result");
  base::DictValue* error = answer.FindDict("error");

  // Result hooks: what the facade must learn from an answer before the client
  // sees it.
  std::vector<ContextEvent> released;
  if (pending.method == "Page.createIsolatedWorld") {
    released = session.contexts.OnIsolatedWorldAnswered(
        result ? result->FindInt("executionContextId") : std::nullopt);
  }
  bool stream_refused = false;
  if (result && !session.streams.RecordResult(pending.method, *result)) {
    stream_refused = true;
    if (const std::string* stream = result->FindString("stream")) {
      base::DictValue close;
      close.Set("handle", *stream);
      SendInternal(session, "IO.close", std::move(close));
    }
  }
  if (result && pending.method == "IO.close" && !pending.stream.empty()) {
    session.streams.Remove(pending.stream);
  }

  if (!pending.client_message_id) {
    return;
  }
  const base::TimeDelta duration = tick_clock_->NowTicks() - pending.start;
  const std::optional<std::string> session_id = session.session_id;
  if (stream_refused) {
    AuditCommand(pending.method, pending.target_id, pending.summary,
                 AuditOutcome::kError, duration);
    ReplyError(*pending.client_message_id, session_id, "Koan.unavailable",
               "too many open streams; close one first");
  } else if (result) {
    AuditCommand(pending.method, pending.target_id, pending.summary,
                 AuditOutcome::kOk, duration);
    if (Redacts(session.target_id)) {
      RedactSecretInputs(pending.method, *result);
    }
    Reply(*pending.client_message_id, session_id, std::move(*result));
  } else {
    AuditCommand(pending.method, pending.target_id, pending.summary,
                 AuditOutcome::kError, duration);
    base::DictValue reply;
    reply.Set("id", *pending.client_message_id);
    reply.Set("sessionId", session.session_id);
    if (error) {
      // Content's own error for the command, as CDP gives it.
      base::DictValue copy;
      if (std::optional<int> code = error->FindInt("code")) {
        copy.Set("code", *code);
      }
      if (const std::string* text = error->FindString("message")) {
        copy.Set("message", *text);
      }
      reply.Set("error", std::move(copy));
    } else {
      reply.Set("result", base::DictValue());
    }
    Send(reply);
  }
  // The events the world's answer released come after the answer, in order.
  for (ContextEvent& event : released) {
    DeliverEvent(session, std::move(event.method), std::move(event.params));
  }
}

void CdpFacade::HandleTabEvent(TabSession& session, base::DictValue event) {
  const std::string* method = event.FindString("method");
  if (!method) {
    return;
  }
  std::string name = *method;
  base::DictValue params;
  if (base::DictValue* given = event.FindDict("params")) {
    params = std::move(*given);
  }
  if (Redacts(session.target_id)) {
    RedactSecretInputs(name, params);
  }
  switch (FilterEvent(name, params)) {
    case EventVerdict::kDeliver:
      DeliverEvent(session, std::move(name), std::move(params));
      return;
    case EventVerdict::kHoldForContextTracker:
      for (ContextEvent& released :
           session.contexts.OnEvent(std::move(name), std::move(params))) {
        DeliverEvent(session, std::move(released.method),
                     std::move(released.params));
      }
      return;
    case EventVerdict::kDrop:
      return;
  }
}

void CdpFacade::DeliverEvent(const TabSession& session,
                             std::string method,
                             base::DictValue params) {
  SendEvent(method, std::move(params), session.session_id);
}

void CdpFacade::SendInternal(TabSession& session,
                             std::string method,
                             base::DictValue params) {
  Forward(std::nullopt, session, method, std::string(), std::move(params));
}

void CdpFacade::Reply(int id,
                      const std::optional<std::string>& session_id,
                      base::DictValue result) {
  base::DictValue message;
  message.Set("id", id);
  message.Set("result", std::move(result));
  if (session_id) {
    message.Set("sessionId", *session_id);
  }
  Send(message);
}

void CdpFacade::ReplyError(int id,
                           const std::optional<std::string>& session_id,
                           std::string_view code,
                           std::string_view text) {
  base::DictValue error;
  error.Set("code", kServerError);
  error.Set("message", text);
  error.Set("data", code);
  base::DictValue message;
  message.Set("id", id);
  message.Set("error", std::move(error));
  if (session_id) {
    message.Set("sessionId", *session_id);
  }
  Send(message);
}

void CdpFacade::SendEvent(std::string_view method,
                          base::DictValue params,
                          const std::optional<std::string>& session_id) {
  base::DictValue message;
  message.Set("method", method);
  message.Set("params", std::move(params));
  if (session_id) {
    message.Set("sessionId", *session_id);
  }
  Send(message);
}

void CdpFacade::Send(const base::DictValue& message) {
  if (std::optional<std::string> json = base::WriteJson(message)) {
    send_.Run(std::move(*json));
  }
}

void CdpFacade::AuditCommand(std::string_view method,
                             std::string_view target_id,
                             std::string summary,
                             AuditOutcome outcome,
                             base::TimeDelta duration) {
  AuditRecord record;
  record.time = base::Time::Now();
  record.event = std::string(method);
  record.client_id = client_id_;
  record.connection_id = connection_id_;
  record.tab_id = std::string(target_id);
  if (!target_id.empty()) {
    if (std::optional<TabFacts> facts = backend_->DescribeTab(target_id)) {
      record.tab_origin = OriginOf(facts->url);
    }
  }
  record.summary = std::move(summary);
  record.outcome = outcome;
  record.duration = duration;
  backend_->Audit(std::move(record));
}

std::string SummarizeCommand(std::string_view method,
                             const base::DictValue& params) {
  auto string_param = [&](std::string_view name) -> const std::string* {
    return params.FindString(name);
  };
  if (method == "Page.navigate" || method == "Target.createTarget" ||
      method == "Koan.openTab") {
    const std::string* url = string_param("url");
    return url ? SummarizeUrl(GURL(*url), /*full_urls=*/false) : std::string();
  }
  if (method == "Runtime.evaluate") {
    const std::string* source = string_param("expression");
    return source ? SummarizeScript(*source) : std::string();
  }
  if (method == "Runtime.callFunctionOn") {
    const std::string* source = string_param("functionDeclaration");
    return source ? SummarizeScript(*source) : std::string();
  }
  if (method == "Page.addScriptToEvaluateOnNewDocument") {
    const std::string* source = string_param("source");
    return source ? SummarizeScript(*source) : std::string();
  }
  if (method == "Input.insertText" || method == "Input.dispatchKeyEvent" ||
      method == "Input.imeSetComposition") {
    const std::string* text = string_param("text");
    return text ? SummarizeText(*text) : std::string();
  }
  return std::string();
}

}  // namespace koan::agent
