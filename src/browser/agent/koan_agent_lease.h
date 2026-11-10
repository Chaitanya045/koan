// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_LEASE_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_LEASE_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/power_monitor/power_observer.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "koan/browser/agent/koan_agent_lease_scheduler.h"
#include "koan/browser/agent/koan_agent_tab_hooks.h"

class PrefService;

// The per-tab keep-alive (.agent/notes/agent-control.md section 8), with no
// upstream patch and no global flag: only tabs an agent is attached to are
// kept awake, and a browser with no agent attached has no lease, no timer and
// no capturer (T-ready-1).
//
// Held for the whole attachment (cheap):
//  - PageLiveStateDecorator::SetIsPinnedTab: never frozen or discarded. The bit
//    is composed with the user's own pin (koan_pinned_tabs.cc reads
//    IsAgentLeased()), never just written;
//  - PageLiveStateDecorator::SetGlicActuationState(kActuatingOnBackgroundTab):
//    the renderer keeps foreground priority (the kGlicActuationPriorityVoter
//    vote), so macOS does not put it in the background class;
//  - on macOS, WebContents::ForbidExternalPopupMenus(): a <select> renders in
//    the page instead of opening a native menu over the user's work;
//  - an agent's own tab is muted (unless the user unmuted it before).
// Held while LeaseScheduler says so (the capturer hold-off, the cap):
//  - a visible WebContents::IncrementCapturerCount(): the tab renders, rAF and
//    CSS animations run, timers are not throttled, screenshots work.
namespace koan::agent {

class AgentTabLease : public content::WebContentsUserData<AgentTabLease>,
                      public content::WebContentsObserver {
 public:
  ~AgentTabLease() override;

  const std::string& lease_id() const { return lease_id_; }
  const std::string& client_id() const { return client_id_; }
  const std::string& task() const { return task_; }
  bool agent_owned() const { return agent_owned_; }
  bool capturing() const { return static_cast<bool>(capturer_); }

 private:
  friend class content::WebContentsUserData<AgentTabLease>;
  friend class AgentLeaseManager;

  AgentTabLease(content::WebContents* contents,
                std::string lease_id,
                std::string client_id,
                std::string task,
                bool agent_owned);

  void SetCapturing(bool capturing);

  // content::WebContentsObserver:
  void WebContentsDestroyed() override;

  const std::string lease_id_;
  const std::string client_id_;
  const std::string task_;
  const bool agent_owned_;
  bool muted_by_lease_ = false;
  bool contents_destroyed_ = false;
  base::ScopedClosureRunner capturer_;
  base::ScopedClosureRunner popup_menus_;
  // Commands waiting for the capturer (the cap was reached).
  std::vector<base::OnceClosure> waiting_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

// Whether an agent holds a lease on `contents`. For the pin composition in
// koan_pinned_tabs.cc: the freezing and discard bit is "pinned by the user or
// leased by an agent".
bool IsAgentLeased(content::WebContents* contents);

// How Koan's pinned tabs say whether the user pinned a tab; registered by the
// pinned-tab code, so that releasing a lease restores the user's own bit.
using UserPinnedQuery = bool (*)(content::WebContents* contents);
void SetUserPinnedQuery(UserPinnedQuery query);

// Owns the scheduler and applies its answers (UI thread). One per browser
// process; nothing runs while no tab is leased.
class AgentLeaseManager : public TabHooksDelegate,
                          public base::PowerStateObserver {
 public:
  static AgentLeaseManager& Get();

  AgentLeaseManager(const AgentLeaseManager&) = delete;
  AgentLeaseManager& operator=(const AgentLeaseManager&) = delete;

  // An agent attached to the tab (its own, or a user tab it was granted).
  void Attach(content::WebContents* contents,
              const std::string& client_id,
              const std::string& task,
              bool agent_owned);
  // The agent detached (released the tab, was revoked, or Agent ready went
  // off). Everything the lease held is released at once.
  void Detach(content::WebContents* contents);
  // Every lease of the client, or every lease.
  void DetachClient(const std::string& client_id);
  void DetachAll();

  // Runs `command` once the tab holds its capturer: at once, or when the queue
  // reaches it (it waits, never fails). Pair it with EndCommand().
  void RunCommand(content::WebContents* contents, base::OnceClosure command);
  void EndCommand(content::WebContents* contents);
  // Holds the capturer open (a wait, a screencast) until EndHold().
  void BeginHold(content::WebContents* contents, base::OnceClosure ready);
  void EndHold(content::WebContents* contents);
  // Koan.keepAlive and `live: always`; nullopt, or why it was refused.
  std::optional<LeaseScheduler::LowPowerRefusal> KeepAlive(
      content::WebContents* contents,
      base::TimeDelta duration);
  std::optional<LeaseScheduler::LowPowerRefusal> SetLiveAlways(
      content::WebContents* contents,
      bool live);

  size_t lease_count() const { return leases_.size(); }

 private:
  friend class AgentTabLease;
  friend class base::NoDestructor<AgentLeaseManager>;

  AgentLeaseManager();
  ~AgentLeaseManager() override;

  // TabHooksDelegate:
  bool IsAgentTab(content::WebContents* contents) override;
  void OnOpenedFromAgentTab(content::WebContents* source,
                            content::WebContents* opened) override;

  // base::PowerStateObserver:
  void OnBatteryPowerStatusChange(
      base::PowerStateObserver::BatteryPowerStatus status) override;

  // The lease's tab is going away.
  void OnLeaseGone(const std::string& lease_id);
  void Apply(LeaseScheduler::Changes changes);
  void OnTick();
  void OnLimitsChanged();
  PowerState CurrentPower() const;
  void StartOrStopTicking();

  // Attached tabs by lease id. Not owned (each is its tab's user data).
  std::map<std::string, raw_ptr<AgentTabLease>> leases_;
  std::unique_ptr<LeaseScheduler> scheduler_;
  base::RepeatingTimer tick_;
  PrefChangeRegistrar registrar_;
  bool on_battery_ = false;
  uint64_t next_id_ = 1;
};

// Whether macOS's Low Power Mode is on (NSProcessInfo; false elsewhere).
bool IsLowPowerModeEnabled();

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_LEASE_H_
