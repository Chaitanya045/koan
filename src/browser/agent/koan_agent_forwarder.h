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
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "koan/browser/agent/koan_agent_audit.h"
#include "koan/browser/agent/koan_agent_auto_resume.h"
#include "koan/browser/agent/koan_agent_autofill_hooks.h"
#include "koan/browser/agent/koan_agent_facade.h"
#include "koan/browser/agent/koan_agent_prompts.h"
#include "koan/browser/agent/koan_agent_registry.h"
#include "koan/browser/agent/koan_agent_session.h"

class GURL;

namespace content {
class RenderFrameHost;
class WebContents;
}  // namespace content

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

class AgentAutofill;
class PauseWatch;
class SecretScrubber;

// A tab an agent holds, as the indicator shows it (design section 6.2): its
// own tab, or a user tab it was given.
struct ControlledTab {
  std::string target_id;
  // The client's label.
  std::string label;
  TabState state = TabState::kDriven;
  bool agent_owned = false;
  // The agent asked to resume (Koan.resume).
  bool resume_asked = false;
  // Paused, and it goes back to the agent by itself once the user stops
  // using the tab (design section 9.2, auto-resume): the pill offers Keep
  // paused.
  bool auto_resumes = false;
  // While the pill counts down ("continues in 5s"): when the tab resumes.
  std::optional<base::TimeTicks> countdown_ends;
  raw_ptr<content::WebContents> contents = nullptr;
};

// The site write access to a user's tab is limited to: the scheme and
// registrable domain (net::SchemefulSite) of an http(s) URL; empty otherwise.
std::string SiteFor(const GURL& url);

class AgentForwarder : public AutofillHooksDelegate {
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
  ~AgentForwarder() override;

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
  // tab back to the agent, takes a tab of theirs back from it, or takes over
  // an agent's tab (pauses it, as their input in it would).
  void HandBack(const std::string& target_id);
  void TakeBack(const std::string& target_id);
  void TakeOver(const std::string& target_id);
  // The pill's Keep paused: the pause waits for Hand back.
  void KeepPaused(const std::string& target_id);
  // Every tab an agent holds.
  std::vector<ControlledTab> ControlledTabs() const;

  // AutofillHooksDelegate (`koan autofill`):
  bool TakeSuggestions(
      content::WebContents* contents,
      const autofill::AutofillClient::PopupOpenArgs& open_args,
      base::WeakPtr<autofill::AutofillSuggestionDelegate> delegate) override;
  bool RefusesCrossDomainFill(content::WebContents* contents) override;

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
  // Pauses a driven tab; `reason` goes with Koan.paused.
  void Pause(const std::string& target_id, std::string_view reason);
  // Auto-resume (design section 9.2, koan_agent_auto_resume.h). From the
  // watcher: the mouse moved over a paused tab's page, a password or payment
  // field got focus, and the pause's timer fired.
  void OnUserMouseMove(const std::string& target_id);
  void OnSecretFieldFocused(const std::string& target_id);
  void OnPauseTick(const std::string& target_id);
  // The pause waits for Hand back from now on.
  void HoldPause(const std::string& target_id, PauseHold hold);
  // Gives a paused tab back to its agent with Koan.resumed {reason}
  // ("handed-back", "user-idle") once its page said what changed.
  void BeginResume(const std::string& target_id, std::string_view reason);
  void FinishResume(const std::string& target_id,
                    const std::string& reason,
                    uint64_t resume_id,
                    bool changed);
  // Writes the pause's state into the registry (Koan.listTabs); true if what
  // holds it changed (Koan.paused is sent again then). `reason` empty: the
  // one it was paused for.
  bool SyncPause(const std::string& target_id, std::string_view reason);
  PauseWatch* PauseWatchFor(std::string_view target_id) const;
  AgentAutofill* AutofillFor(std::string_view target_id) const;
  AgentAutofill* AutofillFor(content::WebContents* contents) const;
  // The frame a facade session is for: the tab's main frame, or the frame
  // target `frame_id` of a child session; null if gone.
  content::RenderFrameHost* FrameFor(std::string_view target_id,
                                     std::string_view frame_id) const;
  // A secret Koan filled in a tab (`koan autofill`): its client's scrubber
  // takes it. The scrubber is made again from the client's other tabs when a
  // tab leaves (a value lives as long as its tab).
  void OnSecretFilled(const std::string& target_id, const std::string& value);
  void RebuildScrubber(const std::string& client_id);
  const SecretScrubber* ScrubberFor(std::string_view client_id) const;
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
  // Each client's secrets (OnSecretFilled()).
  std::map<std::string, std::unique_ptr<SecretScrubber>, std::less<>>
      scrubbers_;
  base::SequenceBound<AuditLog> audit_;
  base::WeakPtrFactory<AgentForwarder> weak_factory_{this};
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_FORWARDER_H_
