// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_SERVICE_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_SERVICE_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/threading/sequence_bound.h"
#include "components/prefs/pref_change_registrar.h"
#include "koan/browser/agent/koan_agent_session.h"

class PrefService;

namespace content {
class WebContents;
}

namespace koan::agent {
class AgentForwarder;
struct ControlledTab;
struct HeldPrompt;
}  // namespace koan::agent

// Agent control in the browser process (.agent/notes/agent-control.md
// sections 4.1, 6.1 and 6.2): starts the gateway while "Agent ready" is on and
// policy allows it, stops it at once when either turns off or the app quits,
// applies the limits from the settings, shows the approval dialog, and keeps
// the list of connected clients that the indicator in every window shows.
//
// Created by the first browser window (KoanBrowserViewController), on the UI
// thread, and alive until the app terminates. The gateway itself runs on the
// IO thread (GatewayHost in the .cc); this class only talks to it by posting.
namespace koan::agent {

struct ConnectedClient {
  uint64_t connection_id = 0;
  std::string client_id;
  std::string label;
};

class AgentControlService {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // The connected clients changed, or the gateway started or stopped.
    virtual void OnAgentControlChanged() = 0;
  };

  // The one service; created on first use (UI thread). Null before that and
  // once the app is terminating.
  static AgentControlService* GetOrCreate();
  static AgentControlService* Get();

  AgentControlService(const AgentControlService&) = delete;
  AgentControlService& operator=(const AgentControlService&) = delete;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  bool listening() const { return listening_; }
  const std::vector<ConnectedClient>& clients() const { return clients_; }

  // "Stop all agents": every connection is closed and every pairing
  // forgotten; the gateway keeps listening if Agent ready is on.
  void StopAllAgents();

  // The tabs the indicator offers a button for (design section 9.2), and the
  // buttons: hand a paused tab back to its agent, take a user tab back.
  std::vector<ControlledTab> controlled_tabs() const;
  void HandBack(const std::string& target_id);
  void TakeBack(const std::string& target_id);
  // What pages in agent tabs wait to show the user (design section 9.4), and
  // the indicator's Open and Dismiss.
  std::vector<HeldPrompt> user_prompts() const;
  void OpenPrompt(uint64_t id);
  void DismissPrompt(uint64_t id);

 private:
  class GatewayHost;
  friend class GatewayHost;

  explicit AgentControlService(PrefService* local_state);
  ~AgentControlService();

  // Starts, stops or updates the gateway to match the prefs and the policy.
  void Update();
  void Shutdown();

  // From the gateway host, posted to the UI thread.
  void OnListening(bool listening);
  void OnClientConnected(ConnectedClient client);
  void OnClientDisconnected(uint64_t connection_id);
  void OnClientMessage(uint64_t connection_id, AgentMessage message);
  // From the forwarder: posted to the gateway.
  void SendToClient(uint64_t connection_id, std::string json);
  void ShowApproval(ApprovalRequest request,
                    base::OnceCallback<void(ApprovalDecision)> answer);
  void CancelApproval(uint64_t request_id);
  void NotifyObservers();

  const raw_ptr<PrefService> local_state_;
  PrefChangeRegistrar registrar_;
  base::CallbackListSubscription terminating_;
  base::SequenceBound<GatewayHost> host_;
  // The facades, the registry and the DevTools clients (UI thread).
  std::unique_ptr<AgentForwarder> forwarder_;
  bool listening_ = false;
  std::vector<ConnectedClient> clients_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<AgentControlService> weak_factory_{this};
};

// The native dialog (koan_agent_approval_dialog.cc). Runs `answer` once:
// approved with the capabilities the user left on, or denied (Deny, Esc,
// closing the window, or Cancel).
void ShowApprovalDialog(const ApprovalRequest& request,
                        base::OnceCallback<void(ApprovalDecision)> answer);
void CancelApprovalDialog(uint64_t request_id);

// The take-over prompt (design section 9.2) in the window of `contents`:
// "<label> wants to control this tab on <site>", and whether that is on this
// site only or wherever the tab goes (`any_site`). Runs `answer` once with the
// user's choice (Esc and closing deny), unless cancelled first. Returns the id
// to cancel it with.
uint64_t ShowTakeOverDialog(content::WebContents* contents,
                            const std::string& label,
                            const std::string& site,
                            bool any_site,
                            base::OnceCallback<void(bool)> answer);
void CancelTakeOverDialog(uint64_t dialog_id);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_SERVICE_H_
