// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_lease.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/power_monitor/power_monitor.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "components/performance_manager/public/decorators/page_live_state_decorator.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/buildflags.h"
#include "koan/browser/agent/koan_agent_limits.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/koan_prefs.h"
#include "ui/gfx/geometry/size.h"

namespace koan::agent {

namespace {

UserPinnedQuery g_user_pinned = nullptr;

using performance_manager::GlicActuationState;
using performance_manager::PageLiveStateDecorator;

int LeaseLimitSetting(PrefService* local_state) {
  return ReadLimit(
      local_state ? &local_state->GetValue(prefs::kAgentControlMaxLeasedTabs)
                  : nullptr,
      kLeasedTabsLimit);
}

bool HalvesInLowPower(PrefService* local_state) {
  return ReadSwitch(
      local_state ? &local_state->GetValue(prefs::kAgentControlLowPowerHalves)
                  : nullptr,
      true);
}

PrefService* LocalState() {
  return g_browser_process ? g_browser_process->local_state() : nullptr;
}

}  // namespace

// --- AgentTabLease -----------------------------------------------------------

AgentTabLease::AgentTabLease(content::WebContents* contents,
                             std::string lease_id,
                             std::string client_id,
                             std::string task,
                             bool agent_owned)
    : content::WebContentsUserData<AgentTabLease>(*contents),
      content::WebContentsObserver(contents),
      lease_id_(std::move(lease_id)),
      client_id_(std::move(client_id)),
      task_(std::move(task)),
      agent_owned_(agent_owned) {
  // The whole-attachment part.
  PageLiveStateDecorator::SetIsPinnedTab(contents, true);
  PageLiveStateDecorator::SetGlicActuationState(
      contents, GlicActuationState::kActuatingOnBackgroundTab);
#if BUILDFLAG(IS_MAC) && BUILDFLAG(USE_EXTERNAL_POPUP_MENU)
  popup_menus_ = contents->ForbidExternalPopupMenus();
#endif
  if (agent_owned_ && !contents->IsAudioMuted()) {
    contents->SetAudioMuted(true);
    muted_by_lease_ = true;
  }
  // A user's tab the agent may read or drive: the agent blocks for passwords,
  // autofill, history, downloads and sync apply while it holds it.
  if (!agent_owned_) {
    containers::SetAgentLeased(contents, true);
  }
}

AgentTabLease::~AgentTabLease() {
  content::WebContents* contents = &GetWebContents();
  capturer_.RunAndReset();
  popup_menus_.RunAndReset();
  if (contents_destroyed_) {
    // The page goes with it: nothing to restore.
    return;
  }
  PageLiveStateDecorator::SetGlicActuationState(contents,
                                                GlicActuationState::kNone);
  PageLiveStateDecorator::SetIsPinnedTab(
      contents, g_user_pinned && g_user_pinned(contents));
  if (muted_by_lease_ && contents->IsAudioMuted()) {
    contents->SetAudioMuted(false);
  }
  if (!agent_owned_) {
    containers::SetAgentLeased(contents, false);
  }
}

void AgentTabLease::SetCapturing(bool capturing) {
  if (capturing == static_cast<bool>(capturer_)) {
    return;
  }
  if (!capturing) {
    capturer_.RunAndReset();
    return;
  }
  // Visible, not hidden-but-painting (design decision 7): the page runs as a
  // visible one and is unfrozen. No wake lock and no "activity".
  capturer_ = GetWebContents().IncrementCapturerCount(
      gfx::Size(), /*stay_hidden=*/false, /*stay_awake=*/false,
      /*is_activity=*/false);
  // Posted: a command may detach the tab, and this runs inside the manager's
  // loop over the scheduler's answer.
  std::vector<base::OnceClosure> waiting = std::move(waiting_);
  for (base::OnceClosure& command : waiting) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, std::move(command));
  }
}

void AgentTabLease::WebContentsDestroyed() {
  contents_destroyed_ = true;
  // Deletes this.
  AgentLeaseManager::Get().Detach(&GetWebContents());
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(AgentTabLease);

bool IsAgentLeased(content::WebContents* contents) {
  return contents && AgentTabLease::FromWebContents(contents);
}

void SetUserPinnedQuery(UserPinnedQuery query) {
  g_user_pinned = query;
}

// --- AgentLeaseManager -------------------------------------------------------

// static
AgentLeaseManager& AgentLeaseManager::Get() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  static base::NoDestructor<AgentLeaseManager> manager;
  return *manager;
}

AgentLeaseManager::AgentLeaseManager() {
  if (PrefService* local_state = LocalState()) {
    registrar_.Init(local_state);
    for (const char* pref : {prefs::kAgentControlMaxLeasedTabs,
                             prefs::kAgentControlLowPowerHalves}) {
      registrar_.Add(pref,
                     base::BindRepeating(&AgentLeaseManager::OnLimitsChanged,
                                         base::Unretained(this)));
    }
  }
  if (base::PowerMonitor* monitor = base::PowerMonitor::GetInstance()) {
    on_battery_ =
        monitor->AddPowerStateObserverAndReturnBatteryPowerStatus(this) ==
        base::PowerStateObserver::BatteryPowerStatus::kBatteryPower;
  }
  scheduler_ = std::make_unique<LeaseScheduler>(LeaseLimitSetting(LocalState()),
                                                HalvesInLowPower(LocalState()),
                                                CurrentPower());
  SetTabHooksDelegate(this);
}

AgentLeaseManager::~AgentLeaseManager() {
  SetTabHooksDelegate(nullptr);
  if (base::PowerMonitor* monitor = base::PowerMonitor::GetInstance()) {
    monitor->RemovePowerStateObserver(this);
  }
}

void AgentLeaseManager::Attach(content::WebContents* contents,
                               const std::string& client_id,
                               const std::string& task,
                               bool agent_owned) {
  if (!contents || AgentTabLease::FromWebContents(contents)) {
    return;
  }
  const std::string id = base::NumberToString(next_id_++);
  AgentTabLease::CreateForWebContents(contents, id, client_id, task,
                                      agent_owned);
  leases_[id] = AgentTabLease::FromWebContents(contents);
  StartOrStopTicking();
}

void AgentLeaseManager::Detach(content::WebContents* contents) {
  AgentTabLease* lease =
      contents ? AgentTabLease::FromWebContents(contents) : nullptr;
  if (!lease) {
    return;
  }
  const std::string id = lease->lease_id();
  leases_.erase(id);
  // The scheduler's answer may give the capturer to a queued tab.
  LeaseScheduler::Changes changes = scheduler_->Detach(id);
  std::erase(changes.release, id);
  // Releases everything the lease held.
  contents->RemoveUserData(AgentTabLease::UserDataKey());
  Apply(std::move(changes));
  StartOrStopTicking();
}

void AgentLeaseManager::DetachClient(const std::string& client_id) {
  std::vector<content::WebContents*> tabs;
  for (const auto& [id, lease] : leases_) {
    if (lease->client_id() == client_id) {
      tabs.push_back(lease->web_contents());
    }
  }
  for (content::WebContents* contents : tabs) {
    Detach(contents);
  }
}

void AgentLeaseManager::DetachAll() {
  std::vector<content::WebContents*> tabs;
  for (const auto& [id, lease] : leases_) {
    tabs.push_back(lease->web_contents());
  }
  for (content::WebContents* contents : tabs) {
    Detach(contents);
  }
}

void AgentLeaseManager::RunCommand(content::WebContents* contents,
                                   base::OnceClosure command) {
  AgentTabLease* lease =
      contents ? AgentTabLease::FromWebContents(contents) : nullptr;
  if (!lease) {
    // Not attached: nothing to keep awake.
    std::move(command).Run();
    return;
  }
  LeaseScheduler::Changes changes;
  const bool now = scheduler_->BeginCommand(lease->lease_id(), changes);
  if (!now) {
    lease->waiting_.push_back(std::move(command));
  }
  Apply(std::move(changes));
  if (now) {
    std::move(command).Run();
  }
}

void AgentLeaseManager::EndCommand(content::WebContents* contents) {
  if (AgentTabLease* lease =
          contents ? AgentTabLease::FromWebContents(contents) : nullptr) {
    scheduler_->EndCommand(lease->lease_id(), base::TimeTicks::Now());
  }
}

void AgentLeaseManager::BeginHold(content::WebContents* contents,
                                  base::OnceClosure ready) {
  AgentTabLease* lease =
      contents ? AgentTabLease::FromWebContents(contents) : nullptr;
  if (!lease) {
    std::move(ready).Run();
    return;
  }
  LeaseScheduler::Changes changes;
  const bool now = scheduler_->BeginHold(lease->lease_id(), changes);
  if (!now) {
    lease->waiting_.push_back(std::move(ready));
  }
  Apply(std::move(changes));
  if (now) {
    std::move(ready).Run();
  }
}

void AgentLeaseManager::EndHold(content::WebContents* contents) {
  if (AgentTabLease* lease =
          contents ? AgentTabLease::FromWebContents(contents) : nullptr) {
    scheduler_->EndHold(lease->lease_id(), base::TimeTicks::Now());
  }
}

std::optional<LeaseScheduler::LowPowerRefusal> AgentLeaseManager::KeepAlive(
    content::WebContents* contents,
    base::TimeDelta duration) {
  AgentTabLease* lease =
      contents ? AgentTabLease::FromWebContents(contents) : nullptr;
  if (!lease) {
    return std::nullopt;
  }
  LeaseScheduler::Changes changes;
  auto refusal = scheduler_->KeepAlive(lease->lease_id(), duration,
                                       base::TimeTicks::Now(), changes);
  Apply(std::move(changes));
  return refusal;
}

std::optional<LeaseScheduler::LowPowerRefusal> AgentLeaseManager::SetLiveAlways(
    content::WebContents* contents,
    bool live) {
  AgentTabLease* lease =
      contents ? AgentTabLease::FromWebContents(contents) : nullptr;
  if (!lease) {
    return std::nullopt;
  }
  LeaseScheduler::Changes changes;
  auto refusal = scheduler_->SetLiveAlways(lease->lease_id(), live, changes);
  Apply(std::move(changes));
  return refusal;
}

bool AgentLeaseManager::IsAgentTab(content::WebContents* contents) {
  return IsAgentLeased(contents);
}

void AgentLeaseManager::OnOpenedFromAgentTab(content::WebContents* source,
                                             content::WebContents* opened) {
  AgentTabLease* lease = AgentTabLease::FromWebContents(source);
  if (!lease) {
    return;
  }
  if (opened_tab_handler_) {
    opened_tab_handler_.Run(source, opened);
    return;
  }
  // It belongs to the same agent and task. A tab a page opens from a user's
  // tab the agent drives in place is the agent's own: the user did not open
  // it.
  Attach(opened, lease->client_id(), lease->task(), /*agent_owned=*/true);
}

void AgentLeaseManager::SetOpenedTabHandler(OpenedTabHandler handler) {
  opened_tab_handler_ = std::move(handler);
}

void AgentLeaseManager::OnBatteryPowerStatusChange(
    base::PowerStateObserver::BatteryPowerStatus status) {
  on_battery_ =
      status == base::PowerStateObserver::BatteryPowerStatus::kBatteryPower;
  Apply(scheduler_->SetPower(CurrentPower(), base::TimeTicks::Now()));
}

void AgentLeaseManager::Apply(LeaseScheduler::Changes changes) {
  for (const std::string& id : changes.release) {
    if (auto it = leases_.find(id); it != leases_.end()) {
      it->second->SetCapturing(false);
    }
  }
  for (const std::string& id : changes.acquire) {
    if (auto it = leases_.find(id); it != leases_.end()) {
      it->second->SetCapturing(true);
    }
  }
}

void AgentLeaseManager::OnTick() {
  // Low Power Mode has no notification in Chromium; it is read while leases
  // exist, once a second (a property read).
  const PowerState power = CurrentPower();
  if (power != scheduler_->power()) {
    Apply(scheduler_->SetPower(power, base::TimeTicks::Now()));
  }
  Apply(scheduler_->Tick(base::TimeTicks::Now()));
}

void AgentLeaseManager::OnLimitsChanged() {
  Apply(scheduler_->SetLimit(LeaseLimitSetting(LocalState()),
                             HalvesInLowPower(LocalState()),
                             base::TimeTicks::Now()));
}

PowerState AgentLeaseManager::CurrentPower() const {
  return PowerState{.on_battery = on_battery_,
                    .low_power_mode = IsLowPowerModeEnabled()};
}

void AgentLeaseManager::StartOrStopTicking() {
  if (leases_.empty()) {
    tick_.Stop();
  } else if (!tick_.IsRunning()) {
    tick_.Start(FROM_HERE, base::Seconds(1),
                base::BindRepeating(&AgentLeaseManager::OnTick,
                                    base::Unretained(this)));
  }
}

#if !BUILDFLAG(IS_MAC)
bool IsLowPowerModeEnabled() {
  return false;
}
#endif

}  // namespace koan::agent
