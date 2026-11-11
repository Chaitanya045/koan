// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_FORWARDER_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_FORWARDER_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/functional/function_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "base/timer/timer.h"
#include "koan/browser/agent/koan_agent_audit.h"
#include "koan/browser/agent/koan_agent_facade.h"
#include "koan/browser/agent/koan_agent_prompts.h"
#include "koan/browser/agent/koan_agent_registry.h"
#include "koan/browser/agent/koan_agent_session.h"

class GURL;

namespace content {
class WebContents;
}

// The browser side of CDP forwarding (.agent/notes/agent-control.md sections
// 2, 4 and 7), on the UI thread: a CdpFacade per authenticated connection, the
// registry of which tabs each client may reach, the untrusted DevTools clients
// the facades talk to tabs through, the keep-alive lease of each agent tab, and
// the audit log every command is written to.
//
// Tabs enter the registry only through Koan.openTab / Target.createTarget
// (opened by the AgentTabOpener the agent containers install) or because a page
// in an agent tab opened them; nothing else, so a client never sees a tab of
// the user's it was not given.
namespace koan::agent {

// A tab the user may need to act on (the indicator shows it): a user tab an
// agent controls, or any tab paused by the user's input.
struct ControlledTab {
  std::string target_id;
  // The client's label.
  std::string label;
  TabState state = TabState::kDriven;
  bool agent_owned = false;
  // The agent asked to resume (Koan.resume).
  bool resume_asked = false;
  raw_ptr<content::WebContents> contents = nullptr;
};

// The site write access to a user's tab is limited to: the scheme and
// registrable domain (net::SchemefulSite) of an http(s) URL; empty otherwise.
std::string SiteFor(const GURL& url);

class AgentForwarder {
 public:
  // Sends one JSON message to a connection (posted to the gateway).
  using SendCallback =
      base::RepeatingCallback<void(uint64_t connection_id, std::string json)>;

  // `log_dir` is where the audit log is written; `on_changed` runs when
  // ControlledTabs() may have changed.
  AgentForwarder(SendCallback send,
                 base::FilePath log_dir,
                 base::RepeatingClosure on_changed);
  AgentForwarder(const AgentForwarder&) = delete;
  AgentForwarder& operator=(const AgentForwarder&) = delete;
  ~AgentForwarder();

  void OnClientConnected(uint64_t connection_id,
                         const std::string& client_id,
                         const std::string& label);
  void OnClientMessage(uint64_t connection_id, AgentMessage message);
  void OnClientDisconnected(uint64_t connection_id);

  // Stop all agents or Agent ready off (`end_tasks`), or the app quits: every
  // connection's facade goes, every tab leaves the registry and every lease is
  // released. With `end_tasks` every task's container ends too
  // (containers::EndAgentTask(): its tabs close and a throwaway container is
  // cleared).
  void Reset(bool end_tasks);

  const TargetRegistry& registry() const { return registry_; }

  // The indicator's buttons (design section 9.2): the user hands a paused
  // tab back to the agent, or takes a tab of theirs back from it.
  void HandBack(const std::string& target_id);
  void TakeBack(const std::string& target_id);
  std::vector<ControlledTab> ControlledTabs() const;

 private:
  class Connection;
  class TabWatcher;

  Connection* ConnectionFor(uint64_t connection_id,
                            const std::string& client_id);

  // Puts an agent tab of the client into the registry, holds its lease and
  // tells the client's facades. Returns its target id, or empty if it could
  // not (the budgets, a tab of another profile).
  std::string AddAgentTab(content::WebContents* contents,
                          const std::string& client_id,
                          const std::string& task);
  void RemoveTab(const std::string& target_id, bool destroyed);
  // The tasks a client opened tabs for, so that each gets
  // containers::EndAgentTask() when it ends (Koan.endTask), when the client's
  // last connection goes, on Stop and on Agent ready off.
  // False if the task is open already with another saved container (or with
  // one, and now none is named, or the other way round).
  bool NoteTask(const std::string& client_id,
                const std::string& task,
                const std::string& saved_container);
  // A task whose first tab could not open is forgotten, so that it can be
  // asked for again with another container.
  void ForgetTaskIfEmpty(const std::string& client_id, const std::string& task);
  void EndTask(const std::string& client_id, const std::string& task);
  void EndClient(const std::string& client_id);
  // From the watcher: the tab closed, or its title or URL changed.
  void OnTabDestroyed(const std::string& target_id);
  void OnTabChanged(const std::string& target_id);
  // A page in a tab the agent holds asked for the user, or no longer does
  // (AgentPageSurfaces).
  void OnPageSurface(content::WebContents* contents,
                     PageSurface surface,
                     bool held);
  // A page in an agent tab opened `opened` (AgentLeaseManager).
  void OnOpenedFromAgentTab(content::WebContents* source,
                            content::WebContents* opened);

  // Take-over (design section 9.2), from a facade.
  void RequestTakeOver(const std::string& client_id,
                       const std::optional<std::string>& target_id,
                       FacadeBackend::OpenTabCallback done);
  void OnTakeOverAnswer(std::string target_id,
                        uint64_t request_id,
                        bool allowed);
  bool ReleaseUserTab(const std::string& target_id);
  void AskToResume(const std::string& target_id);
  // From the watcher: the user's own input in the tab, and a committed
  // navigation of it.
  void OnUserInput(const std::string& target_id);
  void OnTabNavigated(const std::string& target_id, const GURL& url);

  void ForEachFacadeOf(std::string_view client_id,
                       base::FunctionRef<void(CdpFacade&)> run);
  content::WebContents* ContentsFor(std::string_view target_id) const;
  void AuditEvent(std::string_view event,
                  const std::string& target_id,
                  const std::string& client_id);
  void Audit(AuditRecord record);
  void NotifyChanged();

  struct TakeOverRequest {
    uint64_t request_id = 0;
    uint64_t dialog_id = 0;
    std::string client_id;
    std::string site;
    // "Limit a taken-over tab to its site" was off when the user was asked.
    bool any_site = false;
    base::WeakPtr<content::WebContents> contents;
    FacadeBackend::OpenTabCallback done;
    base::OneShotTimer timer;
  };

  const SendCallback send_;
  const base::RepeatingClosure on_changed_;
  TargetRegistry registry_;
  std::map<uint64_t, std::unique_ptr<Connection>> connections_;
  std::map<std::string, std::unique_ptr<TabWatcher>, std::less<>> tabs_;
  std::map<std::string, std::string, std::less<>> labels_;
  // Each client's tasks, and the saved container each was opened in (empty
  // for a throwaway one).
  std::map<std::string, std::map<std::string, std::string>, std::less<>> tasks_;
  // Open take-over prompts, by target id: one per tab.
  std::map<std::string, std::unique_ptr<TakeOverRequest>, std::less<>>
      take_overs_;
  uint64_t next_take_over_id_ = 1;
  std::set<std::string, std::less<>> resume_asked_;
  base::SequenceBound<AuditLog> audit_;
  base::WeakPtrFactory<AgentForwarder> weak_factory_{this};
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_FORWARDER_H_
