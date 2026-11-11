// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_tab_hooks.h"

#include <stdint.h>

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/memory/ref_counted.h"
#include "base/task/sequenced_task_runner.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents.h"
#include "net/http/http_response_headers.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"

namespace koan::agent {

namespace {

TabHooksDelegate* g_delegate = nullptr;
AgentTabOpener* g_opener = nullptr;
PageSurfaceDelegate* g_surfaces = nullptr;

// The delegate if `contents` is held by an agent, else null.
PageSurfaceDelegate* SurfacesFor(content::WebContents* contents) {
  return g_surfaces && contents && g_surfaces->IsHeldByAgent(contents)
             ? g_surfaces
             : nullptr;
}

// A device chooser held for the user. The requester (UsbChooser, HidChooser,
// the Serial and Bluetooth choosers) keeps the close closure for as long as
// its request lives, and runs it when the request ends.
class HeldDeviceChooser : public base::RefCounted<HeldDeviceChooser> {
 public:
  explicit HeldDeviceChooser(ShowDeviceChooser show) : show_(std::move(show)) {}

  void set_id(uint64_t id) { id_ = id; }

  // The user opened (`show`) or dismissed the prompt.
  void Run(content::WeakDocumentPtr owner, bool show) {
    id_ = 0;
    if (show_) {
      close_ = std::move(show_).Run(show ? owner.AsRenderFrameHostIfValid()
                                         : nullptr);
    }
  }

  // The request ended.
  void Close() {
    if (show_) {
      if (id_ && g_surfaces) {
        g_surfaces->ForgetPicker(std::exchange(id_, 0));
      }
      // The controller goes later, as when Chrome's dialog closes: it may
      // answer the requester, which is closing now.
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::DoNothingWithBoundArgs(std::move(show_)));
    } else if (close_) {
      std::move(close_).Run();
    }
  }

 private:
  friend class base::RefCounted<HeldDeviceChooser>;
  ~HeldDeviceChooser() = default;

  ShowDeviceChooser show_;
  base::OnceClosure close_;
  uint64_t id_ = 0;
};

}  // namespace

TabHooksDelegate::~TabHooksDelegate() = default;
AgentTabOpener::~AgentTabOpener() = default;
PageSurfaceDelegate::~PageSurfaceDelegate() = default;

HttpAuthRequest::HttpAuthRequest() = default;
HttpAuthRequest::HttpAuthRequest(HttpAuthRequest&&) = default;
HttpAuthRequest& HttpAuthRequest::operator=(HttpAuthRequest&&) = default;
HttpAuthRequest::~HttpAuthRequest() = default;

void SetAgentTabOpener(AgentTabOpener* opener) {
  g_opener = opener;
}

AgentTabOpener* GetAgentTabOpener() {
  return g_opener;
}

void SetTabHooksDelegate(TabHooksDelegate* delegate) {
  g_delegate = delegate;
}

bool OpensInBackgroundForAgent(content::WebContents* source,
                               WindowOpenDisposition* disposition) {
  if (!g_delegate || !source || !g_delegate->IsAgentTab(source)) {
    return false;
  }
  switch (*disposition) {
    case WindowOpenDisposition::kNewForegroundTab:
    case WindowOpenDisposition::kNewPopup:
    case WindowOpenDisposition::kNewWindow:
    case WindowOpenDisposition::kNewPictureInPicture:
    case WindowOpenDisposition::kSingletonTab:
    case WindowOpenDisposition::kSwitchToTab:
    case WindowOpenDisposition::kNewSplitView:
    // Never an Incognito window: agent control has no Incognito (section 13).
    case WindowOpenDisposition::kOffTheRecord:
      *disposition = WindowOpenDisposition::kNewBackgroundTab;
      return true;
    case WindowOpenDisposition::kUnknown:
    case WindowOpenDisposition::kCurrentTab:
    case WindowOpenDisposition::kNewBackgroundTab:
    case WindowOpenDisposition::kSaveToDisk:
    case WindowOpenDisposition::kIgnoreAction:
      return false;
  }
  return false;
}

void OnOpenedFromAgentTab(content::WebContents* source,
                          content::WebContents* opened) {
  if (g_delegate && source && opened && source != opened &&
      g_delegate->IsAgentTab(source)) {
    g_delegate->OnOpenedFromAgentTab(source, opened);
  }
}

void SetPageSurfaceDelegate(PageSurfaceDelegate* delegate) {
  g_surfaces = delegate;
}

content::JavaScriptDialogManager* JavaScriptDialogManagerForAgent(
    content::WebContents* contents) {
  PageSurfaceDelegate* surfaces = SurfacesFor(contents);
  return surfaces ? surfaces->DialogManager() : nullptr;
}

bool HoldExternalProtocolForAgent(
    content::WebContents* contents,
    const GURL& url,
    ui::PageTransition page_transition,
    bool has_user_gesture,
    bool is_in_fenced_frame_tree,
    const std::optional<url::Origin>& initiating_origin,
    content::RenderFrameHost* initiator_document) {
  PageSurfaceDelegate* surfaces = SurfacesFor(contents);
  if (!surfaces) {
    return false;
  }
  surfaces->HoldExternalProtocol(contents, url, page_transition,
                                 has_user_gesture, is_in_fenced_frame_tree,
                                 initiating_origin, initiator_document);
  return true;
}

bool ShouldHoldHttpAuthForAgent(content::WebContents* contents,
                                content::GuestPageHolder* guest,
                                bool do_not_prompt_for_login) {
  return !guest && !do_not_prompt_for_login && SurfacesFor(contents);
}

std::unique_ptr<content::LoginDelegate> HoldHttpAuthForAgent(
    content::WebContents* web_contents,
    content::BrowserContext* browser_context,
    const net::AuthChallengeInfo& auth_info,
    const content::GlobalRequestID& request_id,
    bool is_request_for_primary_main_frame_navigation,
    bool is_request_for_navigation,
    const GURL& url,
    scoped_refptr<net::HttpResponseHeaders> response_headers,
    content::LoginDelegate::LoginAuthRequiredCallback auth_required_callback) {
  // Only after ShouldHoldHttpAuthForAgent() said yes.
  PageSurfaceDelegate* surfaces = SurfacesFor(web_contents);
  CHECK(surfaces);
  HttpAuthRequest request;
  request.web_contents = web_contents;
  request.browser_context = browser_context;
  request.auth_info = auth_info;
  request.request_id = request_id;
  request.is_request_for_primary_main_frame_navigation =
      is_request_for_primary_main_frame_navigation;
  request.is_request_for_navigation = is_request_for_navigation;
  request.url = url;
  request.response_headers = std::move(response_headers);
  request.auth_required_callback = std::move(auth_required_callback);
  return surfaces->HoldHttpAuth(std::move(request));
}

bool HoldPrintForAgent(content::WebContents* contents,
                       base::OnceCallback<void(bool show)> show) {
  PageSurfaceDelegate* surfaces = SurfacesFor(contents);
  return surfaces && surfaces->HoldPrint(contents, std::move(show));
}

bool HoldActivationForAgent(content::WebContents* contents) {
  PageSurfaceDelegate* surfaces = SurfacesFor(contents);
  if (!surfaces) {
    return false;
  }
  surfaces->HoldActivation(contents);
  return true;
}

bool HoldsPickersForAgent(content::RenderFrameHost* frame) {
  content::WebContents* contents =
      frame ? content::WebContents::FromRenderFrameHost(frame) : nullptr;
  PageSurfaceDelegate* surfaces = SurfacesFor(contents);
  return surfaces && surfaces->MayHoldPickers(contents);
}

bool ShouldHoldPickerForAgent(content::RenderFrameHost* frame) {
  return HoldsPickersForAgent(frame) &&
         !g_surfaces->IsShowingHeld(
             content::WebContents::FromRenderFrameHost(frame));
}

bool HoldFileChooserForAgent(content::RenderFrameHost* frame,
                             base::OnceCallback<void(bool show)> show) {
  if (!ShouldHoldPickerForAgent(frame)) {
    return false;
  }
  g_surfaces->HoldPicker(frame, HeldPicker::kFileChooser, std::move(show));
  return true;
}

base::OnceClosure HoldDeviceChooserForAgent(content::RenderFrameHost* owner,
                                            ShowDeviceChooser show) {
  CHECK(ShouldHoldPickerForAgent(owner));
  auto held = base::MakeRefCounted<HeldDeviceChooser>(std::move(show));
  held->set_id(
      g_surfaces->HoldPicker(owner, HeldPicker::kDeviceChooser,
                             base::BindOnce(&HeldDeviceChooser::Run, held,
                                            owner->GetWeakDocumentPtr())));
  return base::BindOnce(&HeldDeviceChooser::Close, held);
}

void HoldScreenShareForAgent(content::RenderFrameHost* frame,
                             const content::MediaStreamRequest& request,
                             content::MediaResponseCallback callback,
                             ShowScreenSharePicker show) {
  CHECK(ShouldHoldPickerForAgent(frame));
  g_surfaces->HoldPicker(
      frame, HeldPicker::kScreenShare,
      base::BindOnce(
          [](content::WeakDocumentPtr document,
             content::MediaStreamRequest request,
             content::MediaResponseCallback callback,
             ShowScreenSharePicker show, bool open) {
            content::RenderFrameHost* frame =
                document.AsRenderFrameHostIfValid();
            content::WebContents* contents =
                frame ? content::WebContents::FromRenderFrameHost(frame)
                      : nullptr;
            if (!contents) {
              std::move(callback).Run(
                  blink::mojom::StreamDevicesSet(),
                  blink::mojom::MediaStreamRequestResult::
                      FAILED_DUE_TO_SHUTDOWN_NO_RFH_IN_HANDLER,
                  /*ui=*/nullptr);
              return;
            }
            if (!open) {
              std::move(callback).Run(blink::mojom::StreamDevicesSet(),
                                      blink::mojom::MediaStreamRequestResult::
                                          PERMISSION_DENIED_BY_USER,
                                      /*ui=*/nullptr);
              return;
            }
            std::move(show).Run(contents, request, std::move(callback));
          },
          frame->GetWeakDocumentPtr(), request, std::move(callback),
          std::move(show)));
}

}  // namespace koan::agent
