// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_TAB_HOOKS_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_TAB_HOOKS_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "content/public/browser/global_request_id.h"
#include "content/public/browser/login_delegate.h"
#include "content/public/browser/media_stream_request.h"
#include "net/base/auth.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace content {
class BrowserContext;
class GuestPageHolder;
class JavaScriptDialogManager;
class RenderFrameHost;
class WebContents;
}  // namespace content

namespace net {
class HttpResponseHeaders;
}  // namespace net

// The calls upstream code makes into agent control
// (.agent/notes/agent-control.md section 14, hook #2), kept in a target that
// depends on //content only so that the tab delegate can use it. The browser
// side (AgentLeaseManager) installs the delegate; with none installed every
// call does nothing.
namespace koan::agent {

class TabHooksDelegate {
 public:
  virtual ~TabHooksDelegate();

  // Whether an agent is attached to `contents` (it holds a lease).
  virtual bool IsAgentTab(content::WebContents* contents) = 0;
  // A page in the agent tab `source` opened `opened` (window.open, a
  // target=_blank link, a popup): it joins the same agent and task.
  virtual void OnOpenedFromAgentTab(content::WebContents* source,
                                    content::WebContents* opened) = 0;
};

// How agent control opens a tab for an agent (Koan.openTab, a task's first
// tab). The agent containers (storage-partition containers in the user's
// profile, a separate change) implement it; agent control only codes against
// this. The tab must open in the background, never activated, never raising
// or creating a focused window (T-focus-1, T-win-1, T-win-2), in the original
// profile (never Incognito).
struct AgentTabRequest {
  std::string client_id;
  // The task, which names the agent container ("Throwaway" by default).
  std::string task;
  // The saved agent container the agent asked for by name, or empty for a
  // throwaway one. The tab opens in it only if the user granted it to
  // `client_id` (Settings > Agent Control, koan_container_grants.h);
  // otherwise no tab opens.
  std::string saved_container;
  // Already checked with IsAllowedNavigationUrl().
  GURL url;
};

class AgentTabOpener {
 public:
  virtual ~AgentTabOpener();

  // Runs `done` with the new tab, or null if none could be opened.
  virtual void OpenAgentTab(
      const AgentTabRequest& request,
      base::OnceCallback<void(content::WebContents*)> done) = 0;
};

// Installs or (with null) removes the opener; null until the containers
// install one, and Koan.openTab answers Koan.unavailable meanwhile. Not owned.
void SetAgentTabOpener(AgentTabOpener* opener);
AgentTabOpener* GetAgentTabOpener();

// Installs or (with null) removes the delegate. Not owned.
void SetTabHooksDelegate(TabHooksDelegate* delegate);

// Design section 9.4: what a page in an agent tab opens goes to a background
// tab, never a foreground tab, a popup window or a new window, so nothing
// takes the user's focus. Changes `*disposition` and returns true when it did.
// BrowserWebContentsDelegate::AddNewContents and OpenURLFromTab call it.
bool OpensInBackgroundForAgent(content::WebContents* source,
                               WindowOpenDisposition* disposition);

// Tells agent control that `opened` came from the agent tab `source`. Does
// nothing if `source` is not an agent tab or `opened` is null.
void OnOpenedFromAgentTab(content::WebContents* source,
                          content::WebContents* opened);

// Design section 9.4: what a page asks to show while an agent holds its tab
// (the agent's own tab, or a user tab it drives) never comes up over the
// user's work. Each is held as a Koan.userNeeded prompt in the agent
// indicator, and shown the way Chrome would show it when the user opens the
// prompt (koan_agent_page_surfaces.h installs this). Each call is made from
// a hook of a few lines in upstream code (docs/REBASE.md).
// The pickers a page in a held tab may ask for. Unlike the surfaces above they
// need the user in front of the tab, so when the agent lets go they are
// cancelled, not shown.
enum class HeldPicker {
  // <input type=file>.
  kFileChooser,
  // A WebUSB, WebHID, Web Serial or Web Bluetooth chooser, or a Bluetooth
  // scanning prompt.
  kDeviceChooser,
  // getDisplayMedia().
  kScreenShare,
};

struct HttpAuthRequest {
  HttpAuthRequest();
  HttpAuthRequest(HttpAuthRequest&&);
  HttpAuthRequest& operator=(HttpAuthRequest&&);
  ~HttpAuthRequest();

  raw_ptr<content::WebContents> web_contents = nullptr;
  raw_ptr<content::BrowserContext> browser_context = nullptr;
  net::AuthChallengeInfo auth_info;
  content::GlobalRequestID request_id;
  bool is_request_for_primary_main_frame_navigation = false;
  bool is_request_for_navigation = false;
  GURL url;
  scoped_refptr<net::HttpResponseHeaders> response_headers;
  content::LoginDelegate::LoginAuthRequiredCallback auth_required_callback;
};

class PageSurfaceDelegate {
 public:
  virtual ~PageSurfaceDelegate();

  // Whether an agent holds `contents` (koan_agent_lease.h).
  virtual bool IsHeldByAgent(content::WebContents* contents) = 0;
  // The dialog manager of a held tab: it holds alert(), confirm(), prompt()
  // and beforeunload.
  virtual content::JavaScriptDialogManager* DialogManager() = 0;
  virtual void HoldExternalProtocol(content::WebContents* contents,
                                    const GURL& url,
                                    ui::PageTransition page_transition,
                                    bool has_user_gesture,
                                    bool is_in_fenced_frame_tree,
                                    const std::optional<url::Origin>& origin,
                                    content::RenderFrameHost* initiator) = 0;
  virtual std::unique_ptr<content::LoginDelegate> HoldHttpAuth(
      HttpAuthRequest request) = 0;
  // `show` runs with true to show the print preview, false to cancel it.
  // Returns false if this print is the one being shown now (`show` is then
  // dropped and the caller goes on).
  virtual bool HoldPrint(content::WebContents* contents,
                         base::OnceCallback<void(bool show)> show) = 0;
  virtual void HoldActivation(content::WebContents* contents) = 0;
  // Whether the pickers of `contents`, which an agent holds, may be held: not
  // in an agent container (they stay refused there).
  virtual bool MayHoldPickers(content::WebContents* contents) = 0;
  // Whether a held surface of `contents` is being shown now (it isn't held
  // again).
  virtual bool IsShowingHeld(content::WebContents* contents) = 0;
  // Holds a picker `frame` asked for; returns its id (0 if refused). `show`
  // runs once, never inside this call: with true to show the picker the way
  // Chrome would (its tab is in front then), with false to cancel it as if the
  // user had.
  virtual uint64_t HoldPicker(content::RenderFrameHost* frame,
                              HeldPicker picker,
                              base::OnceCallback<void(bool show)> show) = 0;
  // The held picker `id` ended on its own (its request went away).
  virtual void ForgetPicker(uint64_t id) = 0;
};

// Installs or (with null) removes the delegate. Not owned.
void SetPageSurfaceDelegate(PageSurfaceDelegate* delegate);

// BrowserWebContentsDelegate::GetJavaScriptDialogManager: Koan's dialog
// manager for a held tab, or null (Chrome's own then).
content::JavaScriptDialogManager* JavaScriptDialogManagerForAgent(
    content::WebContents* contents);

// ChromeContentBrowserClient::HandleExternalProtocol: true if the launch is
// held (the caller returns false: nothing launches now).
bool HoldExternalProtocolForAgent(
    content::WebContents* contents,
    const GURL& url,
    ui::PageTransition page_transition,
    bool has_user_gesture,
    bool is_in_fenced_frame_tree,
    const std::optional<url::Origin>& initiating_origin,
    content::RenderFrameHost* initiator_document);

// ChromeContentBrowserClient::CreateLoginDelegate: whether a sign-in for
// `contents` is held (not for a guest, or one that may not prompt), and the
// delegate that holds it.
bool ShouldHoldHttpAuthForAgent(content::WebContents* contents,
                                content::GuestPageHolder* guest,
                                bool do_not_prompt_for_login);
std::unique_ptr<content::LoginDelegate> HoldHttpAuthForAgent(
    content::WebContents* web_contents,
    content::BrowserContext* browser_context,
    const net::AuthChallengeInfo& auth_info,
    const content::GlobalRequestID& request_id,
    bool is_request_for_primary_main_frame_navigation,
    bool is_request_for_navigation,
    const GURL& url,
    scoped_refptr<net::HttpResponseHeaders> response_headers,
    content::LoginDelegate::LoginAuthRequiredCallback auth_required_callback);

// PrintViewManager::OnScriptedPrintPreviewCallback (window.print()): true if
// the preview is held; `show` then runs later.
bool HoldPrintForAgent(content::WebContents* contents,
                       base::OnceCallback<void(bool show)> show);

// BrowserWebContentsDelegate::ActivateContents (window.focus(), a service
// worker's clients.focus()): true if the tab must not come forward.
bool HoldActivationForAgent(content::WebContents* contents);

// Whether an agent holds the tab of `frame` and its pickers wait in the agent
// indicator. The picker hooks ask this before their own refusal: a picker of
// an agent container, or of an agent's tab no agent holds, stays refused.
bool HoldsPickersForAgent(content::RenderFrameHost* frame);

// Whether a picker `frame` asks for now is held (HoldsPickersForAgent(), and
// it isn't the held picker being shown): the calls below then hold it.
bool ShouldHoldPickerForAgent(content::RenderFrameHost* frame);

// WebContentsImpl::RunFileChooser, through
// BrowserWebContentsDelegate::HoldFileChooser: true if the chooser is held;
// `show` then runs later (true: run it again, false: cancel it).
bool HoldFileChooserForAgent(content::RenderFrameHost* frame,
                             base::OnceCallback<void(bool show)> show);

// chrome::ShowDeviceChooserDialog, after ShouldHoldPickerForAgent(owner): holds
// the chooser. `show` runs later with the frame to show it for, or with null
// to close it as if the user had; it returns what closes the dialog. Returns
// what closes the chooser, as ShowDeviceChooserDialog does.
using ShowDeviceChooser =
    base::OnceCallback<base::OnceClosure(content::RenderFrameHost* frame)>;
base::OnceClosure HoldDeviceChooserForAgent(content::RenderFrameHost* owner,
                                            ShowDeviceChooser show);

// DisplayMediaAccessHandler::HandleRequest, after its checks and
// ShouldHoldPickerForAgent(frame): holds getDisplayMedia()'s picker. `show`
// (the handler's ShowMediaSelectionDialog) runs later, or `callback` gets
// PERMISSION_DENIED_BY_USER, as when the user cancels the picker.
using ShowScreenSharePicker =
    base::OnceCallback<void(content::WebContents* contents,
                            const content::MediaStreamRequest& request,
                            content::MediaResponseCallback callback)>;
void HoldScreenShareForAgent(content::RenderFrameHost* frame,
                             const content::MediaStreamRequest& request,
                             content::MediaResponseCallback callback,
                             ShowScreenSharePicker show);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_TAB_HOOKS_H_
