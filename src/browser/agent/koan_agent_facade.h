// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_FACADE_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_FACADE_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_audit.h"
#include "koan/browser/agent/koan_agent_session.h"
#include "url/gurl.h"

// The CDP facade of one authenticated connection
// (.agent/notes/agent-control.md sections 4.2 to 4.5): what an agent's
// messages meet between the gateway and the tabs, and what the tabs' answers
// and events meet on the way back. Pure logic over JSON; the browser side
// (koan_agent_forwarder.h) attaches the DevTools clients and owns the tabs.
//
// A command to a tab (CDP's flat mode: a message with the sessionId the facade
// handed out at Target.attachToTarget) passes, in order:
//   1. the method policy (Classify(): unlisted = deny; gated methods need a
//      capability v1 never grants; no-ops answer success);
//   2. the registry's MaySend(): the client's own tab only, and on a readable
//      or paused tab only the methods that change nothing;
//   3. the parameter filter the policy row names (ApplyParamFilter()), with the
//      tab's execution contexts and the session's streams;
// and is then re-serialised from the filtered dict, sent with an id of the
// facade's own, and audited when it is answered. An answer goes back with the
// client's id after its result hook (the isolated world it made, the stream it
// opened). An event passes the allowlist and the scrubber (FilterEvent()) and,
// for an execution context's events, the context tracker.
//
// The browser-level methods are the facade's own: Target.* over the tabs in
// the client's scope (nothing else is ever listed or attachable),
// Browser.getVersion, and Koan.listTabs, Koan.openTab and Koan.closeTab.
//
// One facade per connection, on the UI thread; not thread-safe.
namespace koan::agent {

class TargetRegistry;

// What the browser says about a tab in the registry.
struct TabFacts {
  std::string title;
  GURL url;
  // The tab shows a security interstitial. It commits under the original
  // https URL, so attaching stays allowed; the facade must stop the agent from
  // acting on it (it could press "Proceed").
  bool interstitial = false;
};

class FacadeBackend {
 public:
  virtual ~FacadeBackend();

  // The tab's title and URL; nullopt if it is gone.
  virtual std::optional<TabFacts> DescribeTab(std::string_view target_id) = 0;
  // Attaches this connection's untrusted DevTools client to the tab. False if
  // the tab is gone or content refused (a frame the agent may not reach).
  virtual bool AttachTab(const std::string& target_id) = 0;
  virtual void DetachTab(const std::string& target_id) = 0;
  // Sends a command (JSON, with the facade's `command_id`) to the attached
  // tab once its keep-alive lease lets it run. Just before it runs, the
  // backend asks CdpFacade::MayDispatch(): a command the user's input paused
  // meanwhile is not sent but CdpFacade::OnCommandDropped(). Every command
  // sent is answered by OnTabMessage() or ended by OnTabDetached().
  virtual void SendToTab(const std::string& target_id,
                         int command_id,
                         std::string json) = 0;
  // The tab answered a command sent with SendToTab().
  virtual void CommandAnswered(const std::string& target_id) = 0;
  // Opens a background tab for the client in `task` (Koan.openTab,
  // Target.createTarget) and adds it to the registry. `url` passed
  // IsAllowedNavigationUrl() and the registry had room. `saved_container` is
  // the saved container the agent asked for by name (empty for a throwaway
  // one); one the user did not grant it is Koan.notGranted, never a
  // throwaway tab. Runs `done` with the new tab's target id, or with nullopt
  // and the error (code, message).
  using OpenTabCallback =
      base::OnceCallback<void(std::optional<std::string> target_id,
                              std::string error_code,
                              std::string error_message)>;
  virtual void OpenTab(const std::string& task,
                       const std::string& saved_container,
                       const GURL& url,
                       OpenTabCallback done) = 0;
  // Koan.endTask: the task's tabs close and its throwaway container is
  // cleared (containers::EndAgentTask()).
  virtual void EndTask(const std::string& task) = 0;
  // Closes an agent tab of the client. False if it could not.
  virtual bool CloseTab(const std::string& target_id) = 0;
  // Koan.takeOver: asks the user to let the client drive one of their tabs
  // (`target_id`, or the tab they are looking at), limited to that tab's site.
  // On yes the tab is in the registry (kDriven) before `done` runs.
  virtual void TakeOver(const std::optional<std::string>& target_id,
                        OpenTabCallback done) = 0;
  // Koan.release: the client gives a user tab back. False if it is not one.
  virtual bool ReleaseUserTab(const std::string& target_id) = 0;
  // Whether a navigation of a user tab to `url` stays on the site the user
  // allowed (always true for an agent tab).
  virtual bool IsInGrantedSite(const std::string& target_id,
                               const GURL& url) = 0;
  // Koan.resume: the agent cannot resume a paused tab; this shows the user
  // that it asks to.
  virtual void AskToResume(const std::string& target_id) = 0;
  // Browser.getVersion's answer.
  virtual base::DictValue BrowserVersion() = 0;
  virtual void Audit(AuditRecord record) = 0;
  // Something in the tab needs the user, not the agent (design section 9.4):
  // `reason` is "interstitial", ...
  virtual void UserNeeded(const std::string& target_id,
                          std::string_view reason) = 0;
};

class CdpFacade {
 public:
  // Sends one JSON message to the client.
  using SendCallback = base::RepeatingCallback<void(std::string json)>;

  // The task an agent tab opens in when the client names none.
  static constexpr char kDefaultTask[] = "Throwaway";

  CdpFacade(uint64_t connection_id,
            std::string client_id,
            TargetRegistry* registry,
            FacadeBackend* backend,
            const base::TickClock* tick_clock,
            SendCallback send);
  CdpFacade(const CdpFacade&) = delete;
  CdpFacade& operator=(const CdpFacade&) = delete;
  ~CdpFacade();

  // A message from the client.
  void OnClientMessage(AgentMessage message);
  // A message (an answer or an event) from the DevTools client attached to the
  // tab.
  void OnTabMessage(const std::string& target_id, std::string_view json);
  // The tab's DevTools host went away (the tab closed, or navigated where the
  // agent may not attach) or left the client's scope. Its commands in flight
  // are answered with an error and its session ends.
  void OnTabDetached(const std::string& target_id);
  // A tab joined or left the client's scope (opened by the agent or by a page
  // in an agent tab; closed), or its title or URL changed.
  void OnTargetAdded(const std::string& target_id);
  void OnTargetRemoved(const std::string& target_id);
  void OnTargetChanged(const std::string& target_id);
  // A tab's state in the registry changed (paused by the user's input, handed
  // back, left the allowed site): tells the client with `event`
  // ("Koan.paused", "Koan.resumed", "Koan.siteLeft") and `reason`.
  void OnTargetStateChanged(const std::string& target_id,
                            std::string_view event,
                            std::string_view reason);
  // A page in the tab asked for something only the user can answer (design
  // section 9.4; `reason` as koan_agent_prompts.h's ReasonFor()), which waits
  // in the indicator: Koan.userNeeded {targetId, reason}. Once it no longer
  // waits (the user opened or dismissed it, the agent answered the dialog, the
  // page went on): Koan.userNeededDone {targetId, reason}.
  void OnUserNeeded(const std::string& target_id,
                    std::string_view reason,
                    bool needed);

  // Whether a command the facade sent may still run (the tab may have been
  // paused while it waited for the lease), and the answer for one that may
  // not.
  bool MayDispatch(int command_id) const;
  void OnCommandDropped(int command_id);

  bool IsAttached(std::string_view target_id) const;

 private:
  struct TabSession;
  struct Pending;

  // The client's tab sessions by sessionId, and the sessionId of each tab.
  TabSession* SessionFor(std::string_view session_id);
  TabSession* SessionForTarget(std::string_view target_id);

  void HandleBrowserMethod(AgentMessage& message);
  void HandleSessionMethod(AgentMessage& message, TabSession& session);
  void HandleTargetMethod(AgentMessage& message, TabSession* session);
  void HandleKoanMethod(AgentMessage& message);

  void GetTargets(const AgentMessage& message);
  void GetTargetInfo(const AgentMessage& message, TabSession* session);
  void AttachToTarget(const AgentMessage& message);
  void DetachFromTarget(const AgentMessage& message, TabSession* session);
  void SetAutoAttach(const AgentMessage& message, TabSession* session);
  void SetDiscoverTargets(const AgentMessage& message);
  void CloseTarget(const AgentMessage& message);
  void OpenTab(const AgentMessage& message, bool cdp_shape);
  void OnTabOpened(int client_message_id,
                   std::string method,
                   std::optional<std::string> session_id,
                   base::TimeTicks start,
                   std::string summary,
                   std::optional<std::string> target_id,
                   std::string error_code,
                   std::string error_message);
  void ListTabs(const AgentMessage& message);
  void TakeOver(const AgentMessage& message);
  void Release(const AgentMessage& message);
  void Resume(const AgentMessage& message);
  // Whether answers and events from the tab are redacted (a user tab the
  // agent may only read).
  bool Redacts(const std::string& target_id) const;
  // Whether `method` would act on a tab that shows a security interstitial.
  bool BehindInterstitial(const std::string& target_id,
                          std::string_view method) const;

  // Attaches the client's session to the tab and tells it
  // (Target.attachedToTarget). Returns the session, or null.
  TabSession* Attach(const std::string& target_id);
  // Koan.userNeeded {reason: "interstitial"} for `target_id`, once per
  // interstitial.
  void TellUserNeededForInterstitial(const std::string& target_id);
  void EndSession(const std::string& session_id, bool tell_client);

  void HandleTabAnswer(TabSession& session, base::DictValue answer);
  void HandleTabEvent(TabSession& session, base::DictValue event);
  void DeliverEvent(const TabSession& session,
                    std::string method,
                    base::DictValue params);

  // Sends a command to the tab, or to a child in Chromium's session for it,
  // and remembers it until it is answered. `client_message_id` is nullopt for
  // the facade's own commands (their answers are not the client's). False if
  // it could not be encoded.
  bool Forward(std::optional<int> client_message_id,
               TabSession& session,
               const std::string& method,
               std::string summary,
               base::DictValue params);
  // Sends a command of the facade's own to the tab (its answer is not given to
  // the client).
  void SendInternal(TabSession& session,
                    std::string method,
                    base::DictValue params);

  // Child targets (design section 4.4, T-cdp-3): a Target.* event of the tab
  // or of a child. Returns false if it is not one (it is then an ordinary
  // event).
  bool HandleChildTargetEvent(TabSession& session,
                              const base::DictValue& event);
  // The tab's auto-attach attached a child in `parent`: the agent gets it if
  // it is a frame or a dedicated worker on a URL it may reach (content has
  // asked the client's MayAttachTo* already), and it is detached otherwise.
  void OnChildAttached(TabSession& parent, const base::DictValue& params);
  static base::DictValue ChildTargetInfo(const base::DictValue& info);
  // Whether a child at `url` may be the agent's in the tab `target_id`: any
  // reachable one in an agent's own tab, only the granted site in a user's
  // (a one-site lease, design section 9.2).
  bool ChildOnGrantedSite(const std::string& target_id, const GURL& url) const;

  std::optional<base::DictValue> TargetInfo(const std::string& target_id);
  bool InScope(std::string_view target_id) const;

  void Reply(int id,
             const std::optional<std::string>& session_id,
             base::DictValue result);
  void ReplyError(int id,
                  const std::optional<std::string>& session_id,
                  std::string_view code,
                  std::string_view message);
  void SendEvent(std::string_view method,
                 base::DictValue params,
                 const std::optional<std::string>& session_id);
  void Send(const base::DictValue& message);

  // Audits a message that was answered at once (denied, a no-op, a facade
  // method), or a forwarded command when its answer comes.
  void AuditCommand(std::string_view method,
                    std::string_view target_id,
                    std::string summary,
                    AuditOutcome outcome,
                    base::TimeDelta duration);

  const uint64_t connection_id_;
  const std::string client_id_;
  const raw_ptr<TargetRegistry> registry_;
  const raw_ptr<FacadeBackend> backend_;
  const raw_ptr<const base::TickClock> tick_clock_;
  const SendCallback send_;

  std::map<std::string, std::unique_ptr<TabSession>, std::less<>> sessions_;
  std::map<std::string, std::string, std::less<>> session_by_target_;
  // Chromium's session id of each child the client has -> the facade's.
  std::map<std::string, std::string, std::less<>> child_by_chromium_session_;
  // Commands sent to tabs, by the id the facade gave them.
  std::map<int, Pending> pending_;
  int next_command_id_ = 1;
  // Tabs whose interstitial the client was told about (Koan.userNeeded), so
  // that it is told once per interstitial.
  std::set<std::string, std::less<>> interstitial_tabs_;
  bool discover_targets_ = false;
  bool auto_attach_ = false;

  base::WeakPtrFactory<CdpFacade> weak_factory_{this};
};

// The audit summary of a command: the URL it navigates to, the length and hash
// of the script it runs, the length of the text it types; empty for the rest.
// Never the parameters themselves (T20).
std::string SummarizeCommand(std::string_view method,
                             const base::DictValue& params);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_FACADE_H_
