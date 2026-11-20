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
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_audit.h"
#include "koan/browser/agent/koan_agent_autofill_logic.h"
#include "koan/browser/agent/koan_agent_capture.h"
#include "koan/browser/agent/koan_agent_screenshot_mask.h"
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
// Browser.getVersion, and Koan.listTabs, Koan.openTab, Koan.closeTab,
// Koan.keepAlive, Koan.captureScaled and Koan.autofill (sent with a tab's or
// a frame's sessionId).
//
// Screenshots (section 12, "One capture path"): Koan.captureScaled {targetId,
// longEdge?, scale?, clip?, format?, quality?} copies the frame the tab's
// compositor already drew, scaled on the GPU (koan_agent_capture.h), on any
// tab the agent may read: its own, or a user's it was given (driven, read or
// paused). It takes the tab's attached session through the same checks as
// Page.captureScreenshot (scope, state, reach at attach), asks the page for
// its viewport (Page.getLayoutMetrics), plans the image, collects the masks
// to paint over it (CollectCaptureMasks()) and asks the backend for the
// copy. Only an agent's own tab with no surface falls back to
// Page.captureScreenshot with clip and scale (device emulation); a user's
// tab never does.
//
// The secrets in a page the agent drives (design section 4.5, "Secrets Koan
// filled"; `koan autofill`): in a frame where a password or card field holds
// a value, whoever filled it (Koan, the password manager on load, the user
// typing it), script and value reads are refused (Koan.secretField), and
// typing into a field Koan filled too. Before a script read the backend
// reads those fields in their frames' Chrome-only world if anything could
// have changed them since it last did (FacadeBackend::SecretsCurrent()); the
// session's commands wait behind that read, and behind its Input commands
// still in flight, in order;
// secret fields read as filled or empty in DOM and accessibility answers on
// every tab; a screenshot paints over filled card fields (CollectCaptureMasks()
// for Koan.captureScaled, a re-encode for a relay's Page.captureScreenshot),
// and print and screencast wait; and every message to the client passes the
// scrubber (FacadeBackend::Secrets()) as JSON, screenshot data aside.
//
// Keeping a tab rendering between commands (design section 8): a screencast
// (Page.startScreencast until Page.stopScreencast or the session's end) holds
// the tab's lease, and so does Koan.keepAlive {targetId, seconds} (0 to 600;
// answers {holdId}; {holdId, seconds} renews it, seconds 0 ends it), which the
// `koan` CLI takes while it waits on a tab. A keep-alive lapses by itself, so
// one whose client died without ending it costs at most its seconds, and
// every one of the connection's ends with it.
//
// One facade per connection, on the UI thread; not thread-safe.
namespace koan::agent {

class SecretScrubber;
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
  // A screencast of the attached tab opened (`hold`) or closed: while one is
  // open the tab's keep-alive lease keeps it rendering between commands.
  // Counted: every hold is released once, by Page.stopScreencast, a failed
  // Page.startScreencast, the session's end or the connection's. DetachTab()
  // releases whatever is left.
  virtual void HoldTab(const std::string& target_id, bool hold) = 0;
  // Koan.keepAlive: keeps the tab rendering for `duration` under `key`, or
  // ends that keep-alive (a zero `duration`). Returns nullopt, or the limits
  // that refused it (on battery in Low Power Mode).
  struct LowPowerLimits {
    int max_leases = 1;
    int max_keep_alive_seconds = 60;
  };
  virtual std::optional<LowPowerLimits> KeepTabAlive(
      const std::string& target_id,
      const std::string& key,
      base::TimeDelta duration) = 0;
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
  // (`target_id`, or the tab they are looking at), limited to that tab's site
  // only with Settings > Agent Control > "Limit a taken-over tab to its site".
  // On yes the tab is in the registry (kDriven) before `done` runs.
  virtual void TakeOver(const std::optional<std::string>& target_id,
                        OpenTabCallback done) = 0;
  // Koan.release: the client gives a user tab back. False if it is not one.
  virtual bool ReleaseUserTab(const std::string& target_id) = 0;
  // Whether a user tab (its navigation, or a frame or worker in it) may go to
  // `url`: on the site the user allowed, or anywhere for an agent tab and a
  // user tab taken over without the site limit
  // (TargetRegistry::MayReachSite()).
  virtual bool IsInGrantedSite(const std::string& target_id,
                               const GURL& url) = 0;
  // Koan.resume: the agent cannot resume a paused tab; this shows the user
  // that it asks to.
  virtual void AskToResume(const std::string& target_id) = 0;
  // Koan.captureScaled: the size and scale of the tab's view (nullopt if it
  // has none), and a copy of the frame it drew, scaled, masked and encoded
  // as `job` says (koan_agent_surface_capture.h). `done` runs once, also if
  // the tab goes away.
  virtual std::optional<SurfaceGeometry> DescribeSurface(
      const std::string& target_id) = 0;
  virtual void CopySurface(const std::string& target_id,
                           SurfaceCaptureJob job,
                           SurfaceCaptureCallback done) = 0;
  // Browser.getVersion's answer.
  virtual base::DictValue BrowserVersion() = 0;
  virtual void Audit(AuditRecord record) = 0;
  // Something in the tab needs the user, not the agent (design section 9.4):
  // `reason` is "interstitial", ...
  virtual void UserNeeded(const std::string& target_id,
                          std::string_view reason) = 0;

  // The secret fields a session's commands reach (design section 4.5).
  // `frame_id` is the frame target of a child session, empty for the tab's
  // own: the frames are those of its renderer process, whose script can
  // reach one another.
  struct SecretFacts {
    // A secret field in one of those frames holds a value, whoever filled it
    // (Koan, the password manager, the user, the page): script and value
    // reads are refused.
    bool locked = false;
    // A frame did not answer the read of its fields (RefreshSecrets()): it
    // counts as locked.
    bool unknown = false;
    // The focused field holds such a secret: typing into it is refused.
    bool focused_locked = false;
    // A card number, CVC or IBAN Koan filled is anywhere in the tab.
    bool card_filled = false;
    // The backend DOM node ids, in that process, of the fields whose value
    // is a secret (RedactSecretFields()).
    std::set<int> fields;
  };
  virtual SecretFacts SecretsIn(const std::string& target_id,
                                const std::string& frame_id);
  // Whether SecretsIn() is current for those frames: their secret fields
  // were read (in each frame's Chrome-only world, never sent to the agent)
  // since anything that could change them did (input, a fill, a navigation,
  // Hand back). If not, RefreshSecrets() reads them, gives the values it
  // found to the scrubber, and runs `done` once every frame answered or the
  // read timed out (SecretFacts::unknown). `done` may run inside the call.
  virtual bool SecretsCurrent(const std::string& target_id,
                              const std::string& frame_id);
  virtual void RefreshSecrets(const std::string& target_id,
                              const std::string& frame_id,
                              base::OnceClosure done);
  // An Input command the agent sent to the tab was handled: what its fields
  // hold may have changed (a key, or a click that showed a password the
  // password manager filled on load).
  virtual void SecretsMayHaveChanged(const std::string& target_id);
  // The filled card fields a screenshot of the tab paints over: nodes its
  // own session reaches, and frames (frame target ids) in other processes,
  // whose whole box is painted over.
  struct CardFields {
    std::vector<int> nodes;
    std::vector<std::string> frames;
  };
  virtual CardFields CardFieldsOf(const std::string& target_id);
  // Paints `mask` over `png` (base64) and encodes it again as `format`
  // ("png", "jpeg" or "webp", at `quality`) in base64; nullopt if it could
  // not.
  virtual std::optional<std::string> MaskScreenshot(const std::string& png,
                                                    const ScreenshotMask& mask,
                                                    std::string_view format,
                                                    std::optional<int> quality);
  // Koan.autofill on the field `node` (a backend DOM node id in the
  // session's process): lists Chrome's suggestions for it, or fills the one
  // picked through Chrome's own fill (koan_agent_autofill.h). The facade
  // checked the tab is the client's, driven and not behind an interstitial.
  struct AutofillRequest {
    int node = 0;
    bool list = false;
    AutofillPick pick;
  };
  struct AutofillResult {
    // Empty on success.
    std::string error_code;
    std::string error_message;
    base::DictValue result;
    // The audit summary: the field's kind and the row's label, never a
    // value.
    std::string summary;
  };
  using AutofillCallback = base::OnceCallback<void(AutofillResult)>;
  virtual void Autofill(const std::string& target_id,
                        const std::string& frame_id,
                        const AutofillRequest& request,
                        AutofillCallback done);
  // The values Koan filled for the client's tabs, which nothing sent to it
  // may carry; null if none.
  virtual const SecretScrubber* Secrets();
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
  // A paused tab went back to the agent (design section 9.2): the user handed
  // it back ("handed-back") or stopped using it ("user-idle"). Koan.resumed
  // {targetId, state, reason, pausedMs, url, title, navigated, changed}, so
  // that the agent goes on from the page as the user left it.
  struct Resumed {
    std::string reason;
    base::TimeDelta paused_for;
    // A navigation committed in the tab during the pause.
    bool navigated = false;
    // The page changed during the pause: a navigation, or the DOM or a form
    // field's value changed.
    bool changed = false;
  };
  void OnTargetResumed(const std::string& target_id, const Resumed& resumed);
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
  struct Capture;

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
  void KeepAlive(const AgentMessage& message);
  // Koan.captureScaled, step by step: the viewport's answer, the masks, the
  // copy, and (an agent's own tab with no surface) CDP's screenshot.
  void CaptureScaled(const AgentMessage& message);
  void OnCaptureAnswer(uint64_t capture_id,
                       const std::string& method,
                       base::DictValue* result);
  // The masks (design section 4.5): the boxes, in viewport CSS pixels, of the
  // card-number, CVC and IBAN fields Koan filled in the tab, painted over the
  // capture before it is encoded (MaskPixels() maps them into the image:
  // the clip, the zoom, the device scale and the image's scale). None if no
  // card is filled; Koan.secretField if one's box cannot be located, so the
  // capture is refused.
  using CaptureMasksCallback = base::OnceCallback<void(
      base::expected<std::vector<ViewportRect>, CaptureError> boxes)>;
  void CollectCaptureMasks(TabSession& session, CaptureMasksCallback done);
  void CopyCapture(
      uint64_t capture_id,
      base::expected<std::vector<ViewportRect>, CaptureError> boxes);
  void OnSurfaceCopied(uint64_t capture_id, SurfaceCaptureResult result);
  void FinishCapture(uint64_t capture_id,
                     std::optional<CaptureAnswer> answer,
                     std::optional<CaptureError> error);
  void Autofill(const AgentMessage& message);
  void OnAutofilled(int client_message_id,
                    std::string session_id,
                    std::string target_id,
                    base::TimeTicks start,
                    FacadeBackend::AutofillResult result);
  // Script reads wait for the secret fields to be read (design section 4.5):
  // a session's command that must wait, and every one after it, is held and
  // run in order once it may go.
  enum class SecretsWait { kNone, kInput, kRead };
  SecretsWait MustWait(const TabSession& session, std::string_view method);
  void DispatchSessionMethod(AgentMessage message, TabSession& session);
  void RunHeld(const std::string& session_id);
  void OnSecretsRefreshed(const std::string& session_id);
  // An Input command of the tab was answered or dropped.
  void OnInputDone(const std::string& target_id);
  bool InputInFlight(std::string_view target_id) const;
  // Why `method` may not run in `session` while a secret is in its page, or
  // nullopt.
  std::optional<std::string_view> SecretRefusal(const TabSession& session,
                                                std::string_view method,
                                                const base::DictValue& params);
  // The frame target id the backend knows a session by (empty: the tab's).
  static std::string FrameIdOf(const TabSession& session);
  // Hides the secret fields in a DOM or Accessibility answer or event.
  void RedactFields(const TabSession& session,
                    std::string_view method,
                    base::DictValue& params,
                    bool answer);
  // Where the card fields Koan filled in the session's tab are, in viewport
  // CSS px (DOM.getContentQuads; a field in another process: its frame's
  // <iframe>, DOM.getFrameOwner). A field with no box is not on the screen
  // and is left out; a frame whose owner cannot be found is an error (why,
  // for the agent), and nothing may be captured.
  using FieldBoxesCallback = base::OnceCallback<void(
      base::expected<std::vector<CssRect>, std::string> boxes)>;
  struct FieldMeasure;
  void MeasureCardFields(TabSession& session, FieldBoxesCallback done);
  void OnMeasureOwner(uint64_t measure_id, const base::DictValue* result);
  void MeasureFieldQuads(uint64_t measure_id);
  void OnMeasureQuads(uint64_t measure_id, const base::DictValue* result);
  void FinishMeasure(uint64_t measure_id,
                     base::expected<std::vector<CssRect>, std::string> boxes);
  // A relay client's raw Page.captureScreenshot of a tab with a card field
  // Koan filled: the fields are measured, the screenshot taken as PNG, and
  // its boxes painted over (decoded and encoded again) before the client
  // sees it. Koan.captureScaled paints in the bitmap instead.
  struct ScreenshotJob;
  void StartMaskedScreenshot(const AgentMessage& message,
                             TabSession& session,
                             std::string summary);
  void OnJobMetrics(uint64_t job_id, const base::DictValue* result);
  void OnJobBoxes(uint64_t job_id,
                  base::expected<std::vector<CssRect>, std::string> boxes);
  void CaptureForJob(uint64_t job_id);
  void FinishJob(uint64_t job_id, base::DictValue* result);
  void FailJob(uint64_t job_id, std::string_view why);
  // The session's screencast closed (or never opened): its hold goes.
  void EndScreencastHold(TabSession& session);
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
               base::DictValue params,
               base::OnceCallback<void(const base::DictValue*)> on_answer = {},
               uint64_t mask_job = 0,
               uint64_t capture_id = 0);
  // Sends a command of the facade's own to the tab (its answer is not given to
  // the client). `on_answer`, if any, gets the result (null: an error, or
  // the session ended).
  using InternalAnswer = base::OnceCallback<void(const base::DictValue*)>;
  void SendInternal(TabSession& session,
                    std::string method,
                    base::DictValue params,
                    InternalAnswer on_answer = InternalAnswer());

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

  // `binary`: the result's `data` is an image or a PDF, which the scrubber
  // passes over.
  void Reply(int id,
             const std::optional<std::string>& session_id,
             base::DictValue result,
             bool binary = false);
  void ReplyError(int id,
                  const std::optional<std::string>& session_id,
                  std::string_view code,
                  std::string_view message);
  void SendEvent(std::string_view method,
                 base::DictValue params,
                 const std::optional<std::string>& session_id,
                 bool binary = false);
  // Serializes `message`, scrubs it of the client's secrets and sends it.
  // `binary`: its result's or params' `data` is left out of the scrub.
  void Send(base::DictValue message, bool binary = false);

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
  // The connection's keep-alives (Koan.keepAlive) by hold id: the tab and
  // when it lapses.
  struct KeepAliveHold {
    std::string target_id;
    base::TimeTicks until;
  };
  std::map<std::string, KeepAliveHold, std::less<>> keep_alives_;
  uint64_t next_hold_ = 1;
  // Koan.captureScaled requests in progress, by an id of the facade's own.
  std::map<uint64_t, Capture> captures_;
  uint64_t next_capture_ = 1;
  std::map<uint64_t, std::unique_ptr<ScreenshotJob>> screenshot_jobs_;
  uint64_t next_job_ = 1;
  std::map<uint64_t, std::unique_ptr<FieldMeasure>> measures_;
  uint64_t next_measure_ = 1;
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
