// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_pause_watch.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/values.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "components/autofill/content/browser/content_autofill_client.h"
#include "components/autofill/core/browser/autofill_field.h"
#include "components/autofill/core/browser/form_structure.h"
#include "components/permissions/permission_request_manager.h"
#include "components/security_interstitials/content/security_interstitial_tab_helper.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/agent/koan_agent_page_surfaces.h"
#include "koan/browser/agent/koan_agent_secret_fields.h"

namespace koan::agent {

namespace {

// How long the frames have to say whether they changed before the resume
// goes on without them.
constexpr base::TimeDelta kChangeQueryTimeout = base::Milliseconds(300);
// How often a pause that may resume by itself is looked at (whether the user
// is on the tab, what holds it, the pill's countdown).
constexpr base::TimeDelta kTick = base::Seconds(1);

// In each frame's Chrome-internal isolated world (the page cannot see it):
// from the pause on, note any change to the document or a form field's
// value. Attributes are watched only where they carry state a user changes.
constexpr char16_t kStartWatchScript[] =
    uR"((() => {
  const old = window.__koanPauseWatch;
  if (old) old.stop();
  const state = {changed: false};
  const mark = () => { state.changed = true; };
  const observer = new MutationObserver(mark);
  observer.observe(document, {subtree: true, childList: true,
      characterData: true, attributeFilter: ['value', 'checked', 'selected',
      'open', 'hidden', 'disabled', 'aria-expanded', 'aria-checked',
      'aria-selected']});
  document.addEventListener('input', mark, true);
  document.addEventListener('change', mark, true);
  state.stop = () => {
    observer.disconnect();
    document.removeEventListener('input', mark, true);
    document.removeEventListener('change', mark, true);
  };
  window.__koanPauseWatch = state;
  return true;
})())";

// Whether the frame changed since kStartWatchScript (true if it never ran
// there), and the watch stops.
constexpr char16_t kQueryWatchScript[] =
    uR"((() => {
  const state = window.__koanPauseWatch;
  if (!state) return true;
  state.stop();
  window.__koanPauseWatch = null;
  return state.changed;
})())";

void StopWatching(content::RenderFrameHost* frame) {
  if (frame->IsRenderFrameLive()) {
    frame->ExecuteJavaScriptInIsolatedWorld(kQueryWatchScript,
                                            base::DoNothing(),
                                            ISOLATED_WORLD_ID_CHROME_INTERNAL);
  }
}

}  // namespace

// The frames asked whether they changed, and how many have not answered.
struct PauseWatch::ChangeQuery {
  uint64_t id = 0;
  int waiting = 0;
  bool changed = false;
  base::OnceCallback<void(bool)> done;
  base::OneShotTimer timeout;
};

PauseWatch::PauseWatch(content::WebContents* contents,
                       base::RepeatingClosure on_tick,
                       base::RepeatingClosure on_secret_focus)
    : contents_(contents),
      on_tick_(std::move(on_tick)),
      on_secret_focus_(std::move(on_secret_focus)) {
  // Agent containers' tabs may have no autofill client.
  if (auto* client =
          autofill::ContentAutofillClient::FromWebContents(contents)) {
    autofill_observation_.Observe(
        client, autofill::ScopedAutofillManagersObservation::
                    InitializationPolicy::kObservePreexistingManagers);
  }
}

PauseWatch::~PauseWatch() = default;

void PauseWatch::Begin(std::string_view reason, AutoResumeSettings settings) {
  policy_.SetSettings(settings);
  policy_.Pause(reason, IsUserOnTab(), base::TimeTicks::Now());
  resume_id_ = 0;
  navigated_ = false;
  frames_created_ = false;
  contents_->ForEachRenderFrameHost([](content::RenderFrameHost* frame) {
    if (frame->IsRenderFrameLive()) {
      frame->ExecuteJavaScriptInIsolatedWorld(
          kStartWatchScript, base::DoNothing(),
          ISOLATED_WORLD_ID_CHROME_INTERNAL);
    }
  });
  Rearm();
}

void PauseWatch::End() {
  const bool watching = policy_.paused() && !query_;
  policy_.End();
  resume_id_ = 0;
  timer_.Stop();
  // A watch nobody asked about (Stop, the setting) stops now.
  if (watching) {
    contents_->ForEachRenderFrameHost(&StopWatching);
  }
}

void PauseWatch::Rearm() {
  timer_.Stop();
  if (!policy_.paused() || resuming()) {
    return;
  }
  const PauseHold hold = policy_.hold();
  if (hold != PauseHold::kNone && hold != PauseHold::kSetting) {
    return;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  base::TimeDelta wait = kTick;
  if (std::optional<base::TimeTicks> next = policy_.NextCheck()) {
    wait = std::clamp(*next - now, base::TimeDelta(), kTick);
  }
  timer_.Start(FROM_HERE, wait, on_tick_);
}

void PauseWatch::StopTimer() {
  timer_.Stop();
}

bool PauseWatch::IsUserOnTab() const {
  const tabs::TabInterface* tab =
      tabs::TabInterface::MaybeGetFromContents(contents_);
  if (!tab || !tab->IsVisible()) {
    return false;
  }
  const BrowserWindowInterface* window = tab->GetBrowserWindowInterface();
  return window && window->IsActive();
}

PauseHold PauseWatch::CurrentHold() const {
  content::RenderFrameHost* focused = contents_->GetFocusedFrame();
  if (secret_focused_ || (focused && focused->IsFocusedElementPasswordLike())) {
    return PauseHold::kSecretField;
  }
  if (auto* interstitials = security_interstitials::
          SecurityInterstitialTabHelper::FromWebContents(contents_);
      interstitials && interstitials->IsDisplayingInterstitial()) {
    return PauseHold::kInterstitial;
  }
  for (const HeldPrompt& held : AgentPageSurfaces::Get().Prompts()) {
    if (held.contents == contents_) {
      return PauseHold::kUserNeeded;
    }
  }
  // A held dialog or picker the user opened, or a permission prompt.
  if (AgentPageSurfaces::Get().IsShowingHeld(contents_)) {
    return PauseHold::kDialog;
  }
  if (auto* permissions =
          permissions::PermissionRequestManager::FromWebContents(contents_);
      permissions && permissions->IsRequestInProgress()) {
    return PauseHold::kDialog;
  }
  return PauseHold::kNone;
}

void PauseWatch::OnNavigated() {
  if (policy_.paused()) {
    navigated_ = true;
  }
}

void PauseWatch::OnFrameCreated() {
  if (policy_.paused()) {
    frames_created_ = true;
  }
}

void PauseWatch::QueryChanged(base::OnceCallback<void(bool changed)> done) {
  if (query_) {
    // Only one at a time: the earlier one counts as changed.
    FinishQuery(query_->id, /*timed_out=*/true);
  }
  auto query = std::make_unique<ChangeQuery>();
  query->id = next_id_++;
  query->changed = navigated_ || frames_created_;
  query->done = std::move(done);
  const uint64_t id = query->id;
  query_ = std::move(query);
  contents_->ForEachRenderFrameHost([&](content::RenderFrameHost* frame) {
    if (!frame->IsRenderFrameLive()) {
      return;
    }
    ++query_->waiting;
    frame->ExecuteJavaScriptInIsolatedWorld(
        kQueryWatchScript,
        base::BindOnce(&PauseWatch::OnFrameAnswer, weak_factory_.GetWeakPtr(),
                       id),
        ISOLATED_WORLD_ID_CHROME_INTERNAL);
  });
  if (!query_->waiting) {
    FinishQuery(id, /*timed_out=*/false);
    return;
  }
  query_->timeout.Start(
      FROM_HERE, kChangeQueryTimeout,
      base::BindOnce(&PauseWatch::FinishQuery, weak_factory_.GetWeakPtr(), id,
                     /*timed_out=*/true));
}

void PauseWatch::OnFrameAnswer(uint64_t query_id, base::Value changed) {
  if (!query_ || query_->id != query_id) {
    return;
  }
  // Anything but a plain false (an error, a page that broke the watch) is a
  // change.
  if (!changed.is_bool() || changed.GetBool()) {
    query_->changed = true;
  }
  if (--query_->waiting <= 0) {
    FinishQuery(query_id, /*timed_out=*/false);
  }
}

void PauseWatch::FinishQuery(uint64_t query_id, bool timed_out) {
  if (!query_ || query_->id != query_id) {
    return;
  }
  std::unique_ptr<ChangeQuery> query = std::move(query_);
  const bool changed = query->changed || timed_out;
  std::move(query->done).Run(changed);
}

uint64_t PauseWatch::BeginResume() {
  resume_id_ = next_id_++;
  timer_.Stop();
  return resume_id_;
}

void PauseWatch::OnAfterFocusOnFormField(autofill::AutofillManager& manager,
                                         autofill::FormGlobalId form,
                                         autofill::FieldGlobalId field) {
  const autofill::FormStructure* structure =
      std::as_const(manager).FindCachedFormById(form);
  const autofill::AutofillField* focused =
      structure ? structure->GetFieldById(field) : nullptr;
  secret_focused_ = focused && IsSecretField(*focused);
  if (secret_focused_) {
    on_secret_focus_.Run();
  }
}

void PauseWatch::OnAfterFocusOnNonFormField(
    autofill::AutofillManager& manager) {
  secret_focused_ = false;
}

}  // namespace koan::agent
