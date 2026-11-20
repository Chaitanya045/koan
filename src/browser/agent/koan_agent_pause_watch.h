// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_PAUSE_WATCH_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_PAUSE_WATCH_H_

#include <stdint.h>

#include <memory>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/autofill/core/browser/foundations/autofill_manager.h"
#include "components/autofill/core/browser/foundations/scoped_autofill_managers_observation.h"
#include "koan/browser/agent/koan_agent_auto_resume.h"

namespace content {
class WebContents;
}

// What the browser knows about a tab in the agent registry that auto-resume
// (koan_agent_auto_resume.h, design section 9.2) needs: whether the user is on
// the tab, what holds a pause (a password or payment field with focus, a
// page's prompt, an interstitial, a dialog or a permission prompt), and
// whether the page changed during a pause. It also keeps the tab's
// AutoResumePolicy and the timer that moves it on. One per tab, owned by the
// forwarder's TabWatcher, UI thread.
namespace koan::agent {

class PauseWatch final : public autofill::AutofillManager::Observer {
 public:
  // `on_tick` runs when the policy should be moved on (Advance()), and once a
  // second while the pill counts down. `on_secret_focus` runs when a
  // password, credit-card or payment field of the page gets focus.
  PauseWatch(content::WebContents* contents,
             base::RepeatingClosure on_tick,
             base::RepeatingClosure on_secret_focus);
  PauseWatch(const PauseWatch&) = delete;
  PauseWatch& operator=(const PauseWatch&) = delete;
  ~PauseWatch() override;

  AutoResumePolicy& policy() { return policy_; }
  const AutoResumePolicy& policy() const { return policy_; }

  // The tab was paused: the policy starts and the page's frames are watched
  // for changes from now on.
  void Begin(std::string_view reason, AutoResumeSettings settings);
  // The pause ended (resumed, or nothing to resume any more).
  void End();
  // Arms the timer for the policy's next check (at most a second away while
  // the pause may resume by itself or waits for the setting); stops it while
  // held or not paused.
  void Rearm();
  void StopTimer();

  // Whether the user is on the tab: it is shown in its window (the active tab,
  // or a pane of the active split), and that window is the active one (so
  // not minimised, and Koan is the active app).
  bool IsUserOnTab() const;
  // What holds a pause now, as the browser sees it (kNone: nothing).
  PauseHold CurrentHold() const;

  // A navigation committed in the tab's main frame, or a frame was created.
  void OnNavigated();
  void OnFrameCreated();
  bool navigated() const { return navigated_; }

  // Asks the page's frames whether anything changed since Begin() (the DOM,
  // or a form field's value). `done` runs once, within a short time: a frame
  // that does not answer by then, or one created during the pause, counts as
  // changed.
  void QueryChanged(base::OnceCallback<void(bool changed)> done);

  // The resume a check decided, while the page is asked what changed: an id
  // that the user's input ends (a user-idle resume is not made then).
  uint64_t BeginResume();
  void CancelResume() { resume_id_ = 0; }
  bool IsResuming(uint64_t id) const { return id && id == resume_id_; }
  bool resuming() const { return resume_id_ != 0; }

  // autofill::AutofillManager::Observer:
  void OnAfterFocusOnFormField(autofill::AutofillManager& manager,
                               autofill::FormGlobalId form,
                               autofill::FieldGlobalId field) override;
  void OnAfterFocusOnNonFormField(autofill::AutofillManager& manager) override;

 private:
  struct ChangeQuery;
  void OnFrameAnswer(uint64_t query_id, base::Value changed);
  void FinishQuery(uint64_t query_id, bool timed_out);

  const raw_ptr<content::WebContents> contents_;
  const base::RepeatingClosure on_tick_;
  const base::RepeatingClosure on_secret_focus_;
  AutoResumePolicy policy_;
  base::OneShotTimer timer_;
  // Autofill's view of the focused field, for payment fields (the password
  // check asks the focused frame directly).
  bool secret_focused_ = false;
  autofill::ScopedAutofillManagersObservation autofill_observation_{this};
  bool navigated_ = false;
  bool frames_created_ = false;
  std::unique_ptr<ChangeQuery> query_;
  uint64_t next_id_ = 1;
  uint64_t resume_id_ = 0;
  base::WeakPtrFactory<PauseWatch> weak_factory_{this};
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_PAUSE_WATCH_H_
