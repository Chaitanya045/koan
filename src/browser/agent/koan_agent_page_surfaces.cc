// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_page_surfaces.h"

#include <algorithm>
#include <string>
#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/external_protocol/external_protocol_handler.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/login/http_auth_coordinator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/javascript_dialogs/tab_modal_dialog_manager.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/javascript_dialog_manager.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "koan/browser/agent/koan_agent_lease.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "net/http/http_response_headers.h"
#include "ui/base/base_window.h"
#include "url/gurl.h"

namespace koan::agent {

namespace {

uintptr_t KeyFor(content::WebContents* contents) {
  return reinterpret_cast<uintptr_t>(contents);
}

std::string HostOf(content::WebContents* contents) {
  return std::string(contents->GetLastCommittedURL().host());
}

// The user opened a prompt: its tab comes forward in its window, and the
// window to the front, as when the user clicks a tab.
void BringForward(content::WebContents* contents) {
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(contents);
  BrowserWindowInterface* window =
      tab ? tab->GetBrowserWindowInterface() : nullptr;
  if (!window) {
    return;
  }
  TabStripModel* strip = window->GetTabStripModel();
  const int index = strip->GetIndexOfWebContents(contents);
  if (index != TabStripModel::kNoTab) {
    strip->ActivateTabAt(index);
  }
  if (ui::BaseWindow* base_window = window->GetWindow()) {
    base_window->Activate();
  }
}

// A dialog's answer, given at most once whoever answers: the user through
// Chrome's dialog, the indicator's Dismiss, the page cancelling it.
class DialogAnswer : public base::RefCounted<DialogAnswer> {
 public:
  explicit DialogAnswer(
      content::JavaScriptDialogManager::DialogClosedCallback callback)
      : callback_(std::move(callback)) {}

  void Run(bool success, const std::u16string& user_input) {
    if (callback_) {
      std::move(callback_).Run(success, user_input);
    }
  }
  content::JavaScriptDialogManager::DialogClosedCallback Bind() {
    return base::BindOnce(&DialogAnswer::Run, base::WrapRefCounted(this));
  }

 private:
  friend class base::RefCounted<DialogAnswer>;
  ~DialogAnswer() = default;

  content::JavaScriptDialogManager::DialogClosedCallback callback_;
};

javascript_dialogs::TabModalDialogManager* ChromeDialogs(
    content::WebContents* contents) {
  return javascript_dialogs::TabModalDialogManager::FromWebContents(contents);
}

PageSurface SurfaceFor(HeldPicker picker) {
  switch (picker) {
    case HeldPicker::kFileChooser:
      return PageSurface::kFilePicker;
    case HeldPicker::kDeviceChooser:
      return PageSurface::kDeviceChooser;
    case HeldPicker::kScreenShare:
      return PageSurface::kScreenShare;
  }
  return PageSurface::kFilePicker;
}

}  // namespace

// Watches a tab that holds surfaces, so that they go when it closes (and a
// picker when its page goes), and so that a picker the user opened is shown
// once the tab is visible.
class AgentPageSurfaces::TabWatch : public content::WebContentsObserver {
 public:
  TabWatch(AgentPageSurfaces* owner, content::WebContents* contents)
      : content::WebContentsObserver(contents), owner_(owner) {}

  void WebContentsDestroyed() override {
    owner_->OnTabDestroyed(KeyFor(web_contents()));
  }
  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility == content::Visibility::VISIBLE) {
      owner_->OnTabVisible(KeyFor(web_contents()));
    }
  }
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (handle->HasCommitted() && !handle->IsSameDocument()) {
      PostSweep();
    }
  }
  void RenderFrameDeleted(content::RenderFrameHost* frame) override {
    PostSweep();
  }

 private:
  // Later: a page's WeakDocumentPtr is reset after these notifications.
  void PostSweep() {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&AgentPageSurfaces::SweepPickers,
                       base::Unretained(owner_.get()), KeyFor(web_contents())));
  }

  const raw_ptr<AgentPageSurfaces> owner_;
};

// The JavaScriptDialogManager of a tab an agent holds. A dialog it is given is
// held; open shows it through Chrome's own manager for the tab, which then
// owns it (so HandleJavaScriptDialog and CancelDialogs go on to it).
class AgentPageSurfaces::HeldDialogs : public content::JavaScriptDialogManager {
 public:
  explicit HeldDialogs(AgentPageSurfaces* owner) : owner_(owner) {}

  void RunJavaScriptDialog(content::WebContents* web_contents,
                           content::RenderFrameHost* render_frame_host,
                           content::JavaScriptDialogType dialog_type,
                           const std::u16string& message_text,
                           const std::u16string& default_prompt_text,
                           DialogClosedCallback callback,
                           bool* did_suppress_message) override {
    *did_suppress_message = false;
    auto answer = base::MakeRefCounted<DialogAnswer>(std::move(callback));
    PromptQueue::Actions actions;
    actions.replay = base::BindOnce(
        [](base::WeakPtr<content::WebContents> contents,
           content::GlobalRenderFrameHostId frame_id,
           content::JavaScriptDialogType type, std::u16string message,
           std::u16string default_prompt, scoped_refptr<DialogAnswer> answer) {
          content::RenderFrameHost* frame =
              content::RenderFrameHost::FromID(frame_id);
          auto* chrome_dialogs =
              contents ? ChromeDialogs(contents.get()) : nullptr;
          if (!frame || !frame->IsActive() || !chrome_dialogs) {
            answer->Run(false, std::u16string());
            return;
          }
          bool suppressed = false;
          chrome_dialogs->RunJavaScriptDialog(contents.get(), frame, type,
                                              message, default_prompt,
                                              answer->Bind(), &suppressed);
          if (suppressed) {
            answer->Run(false, std::u16string());
          }
        },
        web_contents->GetWeakPtr(), render_frame_host->GetGlobalId(),
        dialog_type, message_text, default_prompt_text, answer);
    actions.dismiss =
        base::BindOnce(&DialogAnswer::Run, answer, false, std::u16string());
    owner_->Hold(web_contents, PageSurface::kJavaScriptDialog, std::string(),
                 std::move(actions));
  }

  void RunBeforeUnloadDialog(content::WebContents* web_contents,
                             content::RenderFrameHost* render_frame_host,
                             bool is_reload,
                             DialogClosedCallback callback) override {
    auto answer = base::MakeRefCounted<DialogAnswer>(std::move(callback));
    PromptQueue::Actions actions;
    actions.replay = base::BindOnce(
        [](base::WeakPtr<content::WebContents> contents,
           content::GlobalRenderFrameHostId frame_id, bool is_reload,
           scoped_refptr<DialogAnswer> answer) {
          content::RenderFrameHost* frame =
              content::RenderFrameHost::FromID(frame_id);
          auto* chrome_dialogs =
              contents ? ChromeDialogs(contents.get()) : nullptr;
          if (!frame || !frame->IsActive() || !chrome_dialogs) {
            answer->Run(false, std::u16string());
            return;
          }
          chrome_dialogs->RunBeforeUnloadDialog(contents.get(), frame,
                                                is_reload, answer->Bind());
        },
        web_contents->GetWeakPtr(), render_frame_host->GetGlobalId(), is_reload,
        answer);
    // Dismissed: stay on the page (the unload is cancelled).
    actions.dismiss =
        base::BindOnce(&DialogAnswer::Run, answer, false, std::u16string());
    owner_->Hold(web_contents, PageSurface::kBeforeUnload, std::string(),
                 std::move(actions));
  }

  bool HandleJavaScriptDialog(content::WebContents* web_contents,
                              bool accept,
                              const std::u16string* prompt_override) override {
    // The agent answered with Page.handleJavaScriptDialog: DevTools already
    // ran the dialog's callback, so a held one just goes.
    const std::vector<uint64_t> held = owner_->DialogsOf(web_contents);
    if (!held.empty()) {
      owner_->Forget(held.front());
      return true;
    }
    auto* chrome_dialogs = ChromeDialogs(web_contents);
    return chrome_dialogs && chrome_dialogs->HandleJavaScriptDialog(
                                 web_contents, accept, prompt_override);
  }

  void CancelDialogs(content::WebContents* web_contents,
                     bool reset_state) override {
    for (uint64_t id : owner_->DialogsOf(web_contents)) {
      owner_->Dismiss(id);
    }
    if (auto* chrome_dialogs = ChromeDialogs(web_contents)) {
      chrome_dialogs->CancelDialogs(web_contents, reset_state);
    }
  }

 private:
  const raw_ptr<AgentPageSurfaces> owner_;
};

// The LoginDelegate content holds for a sign-in in a held tab. Content
// destroys it when the request goes; open starts Chrome's own flow (with
// Koan's HttpAuthCoordinator), which it then owns.
class AgentPageSurfaces::HeldLogin : public content::LoginDelegate {
 public:
  explicit HeldLogin(HttpAuthRequest request) : request_(std::move(request)) {}
  ~HeldLogin() override {
    if (id_) {
      AgentPageSurfaces::Get().Forget(id_);
    }
  }

  void set_id(uint64_t id) { id_ = id; }

  void Show(HttpAuthCoordinator* coordinator) {
    id_ = 0;
    shown_ = coordinator->CreateLoginDelegate(
        request_.web_contents, request_.browser_context, request_.auth_info,
        request_.request_id,
        request_.is_request_for_primary_main_frame_navigation,
        request_.is_request_for_navigation, request_.url,
        request_.response_headers, /*do_not_prompt_for_login=*/false,
        /*guest=*/nullptr, std::move(request_.auth_required_callback));
  }

  // Dismissed: the request goes on without credentials (the page shows its
  // 401 body). This may destroy `this`.
  void Cancel() {
    id_ = 0;
    if (request_.auth_required_callback) {
      std::move(request_.auth_required_callback).Run(std::nullopt);
    }
  }

  base::WeakPtr<HeldLogin> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  HttpAuthRequest request_;
  uint64_t id_ = 0;
  std::unique_ptr<content::LoginDelegate> shown_;
  base::WeakPtrFactory<HeldLogin> weak_factory_{this};
};

// static
AgentPageSurfaces& AgentPageSurfaces::Get() {
  static base::NoDestructor<AgentPageSurfaces> surfaces;
  return *surfaces;
}

AgentPageSurfaces::AgentPageSurfaces()
    : dialog_manager_(std::make_unique<HeldDialogs>(this)) {}

AgentPageSurfaces::~AgentPageSurfaces() = default;

void AgentPageSurfaces::Install(EventCallback on_event,
                                base::RepeatingClosure on_changed) {
  on_event_ = std::move(on_event);
  on_changed_ = std::move(on_changed);
  installed_ = true;
  SetPageSurfaceDelegate(this);
}

void AgentPageSurfaces::Uninstall() {
  SetPageSurfaceDelegate(nullptr);
  installed_ = false;
  on_event_.Reset();
  on_changed_.Reset();
  std::vector<uintptr_t> tabs;
  for (const auto& [tab, watch] : tabs_) {
    tabs.push_back(tab);
  }
  for (uintptr_t tab : tabs) {
    queue_.ReplayTab(tab);
  }
  tabs_.clear();
}

std::vector<HeldPrompt> AgentPageSurfaces::Prompts() const {
  std::vector<HeldPrompt> prompts;
  for (const UserPrompt& prompt : queue_.List()) {
    prompts.push_back({prompt, ContentsOf(prompt.tab)});
  }
  return prompts;
}

void AgentPageSurfaces::Open(uint64_t id) {
  const UserPrompt* prompt = queue_.Find(id);
  if (!prompt) {
    return;
  }
  const uintptr_t tab = prompt->tab;
  const PageSurface surface = prompt->surface;
  content::WebContents* contents = ContentsOf(tab);
  if (contents) {
    BringForward(contents);
  }
  if (NeedsUserInFront(surface) && contents &&
      contents->GetVisibility() != content::Visibility::VISIBLE) {
    // A picker is refused for a tab that isn't visible: shown once it is
    // (OnTabVisible()).
    opening_.insert(id);
    return;
  }
  pickers_.erase(id);
  opening_.erase(id);
  queue_.Open(id);
  Notify(tab, surface, /*held=*/false);
}

void AgentPageSurfaces::Dismiss(uint64_t id) {
  const UserPrompt* prompt = queue_.Find(id);
  if (!prompt) {
    return;
  }
  const uintptr_t tab = prompt->tab;
  const PageSurface surface = prompt->surface;
  pickers_.erase(id);
  opening_.erase(id);
  queue_.Dismiss(id);
  Notify(tab, surface, /*held=*/false);
}

void AgentPageSurfaces::ReleaseTab(content::WebContents* contents) {
  const uintptr_t tab = KeyFor(contents);
  if (!tabs_.contains(tab)) {
    return;
  }
  queue_.ReplayTab(tab);
  if (std::ranges::none_of(queue_.List(), [tab](const UserPrompt& prompt) {
        return prompt.tab == tab;
      })) {
    tabs_.erase(tab);
  }
  if (on_changed_) {
    on_changed_.Run();
  }
}

bool AgentPageSurfaces::IsHeldByAgent(content::WebContents* contents) {
  return installed_ && IsAgentLeased(contents);
}

content::JavaScriptDialogManager* AgentPageSurfaces::DialogManager() {
  return dialog_manager_.get();
}

void AgentPageSurfaces::HoldExternalProtocol(
    content::WebContents* contents,
    const GURL& url,
    ui::PageTransition page_transition,
    bool has_user_gesture,
    bool is_in_fenced_frame_tree,
    const std::optional<url::Origin>& origin,
    content::RenderFrameHost* initiator) {
  PromptQueue::Actions actions;
  actions.replay = base::BindOnce(
      [](GURL url, base::WeakPtr<content::WebContents> contents,
         ui::PageTransition page_transition, bool has_user_gesture,
         bool is_in_fenced_frame_tree, std::optional<url::Origin> origin,
         content::WeakDocumentPtr initiator) {
        if (!contents) {
          return;
        }
        // Chrome's own checks and its "Open <app>?" dialog.
        ExternalProtocolHandler::LaunchUrl(
            url,
            base::BindRepeating(
                [](base::WeakPtr<content::WebContents> contents) {
                  return contents.get();
                },
                contents),
            page_transition, has_user_gesture, is_in_fenced_frame_tree, origin,
            std::move(initiator));
      },
      url, contents->GetWeakPtr(), page_transition, has_user_gesture,
      is_in_fenced_frame_tree, origin,
      initiator ? initiator->GetWeakDocumentPtr() : content::WeakDocumentPtr());
  actions.dismiss = base::DoNothing();
  Hold(contents, PageSurface::kExternalProtocol, std::string(url.scheme()),
       std::move(actions));
}

std::unique_ptr<content::LoginDelegate> AgentPageSurfaces::HoldHttpAuth(
    HttpAuthRequest request) {
  content::WebContents* contents = request.web_contents;
  auto login = std::make_unique<HeldLogin>(std::move(request));
  PromptQueue::Actions actions;
  actions.replay = base::BindOnce(
      [](base::WeakPtr<HeldLogin> login, AgentPageSurfaces* surfaces) {
        if (login) {
          if (!surfaces->http_auth_) {
            surfaces->http_auth_ = std::make_unique<HttpAuthCoordinator>();
          }
          login->Show(surfaces->http_auth_.get());
        }
      },
      login->GetWeakPtr(), base::Unretained(this));
  // Later, never inside CreateLoginDelegate() (a refused sign-in is
  // dismissed at once).
  actions.dismiss = base::BindOnce(
      [](base::WeakPtr<HeldLogin> login) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindOnce(&HeldLogin::Cancel, std::move(login)));
      },
      login->GetWeakPtr());
  // A refused sign-in was cancelled by its dismiss; content still owns the
  // delegate and ends the request.
  login->set_id(Hold(contents, PageSurface::kHttpAuth, std::string(),
                     std::move(actions)));
  return login;
}

bool AgentPageSurfaces::HoldPrint(content::WebContents* contents,
                                  base::OnceCallback<void(bool show)> show) {
  if (showing_ == contents) {
    return false;
  }
  auto [replay, dismiss] = base::SplitOnceCallback(std::move(show));
  PromptQueue::Actions actions;
  actions.replay = base::BindOnce(
      [](AgentPageSurfaces* surfaces,
         base::WeakPtr<content::WebContents> contents,
         base::OnceCallback<void(bool)> show) {
        if (!contents) {
          return;
        }
        base::AutoReset<raw_ptr<content::WebContents>> showing(
            &surfaces->showing_, contents.get());
        std::move(show).Run(true);
      },
      base::Unretained(this), contents->GetWeakPtr(), std::move(replay));
  actions.dismiss = base::BindOnce(std::move(dismiss), false);
  Hold(contents, PageSurface::kPrint, std::string(), std::move(actions));
  return true;
}

void AgentPageSurfaces::HoldActivation(content::WebContents* contents) {
  // Open brings the tab forward; nothing else to show.
  Hold(contents, PageSurface::kFocus, std::string(),
       {base::DoNothing(), base::DoNothing()});
}

bool AgentPageSurfaces::MayHoldPickers(content::WebContents* contents) {
  return !koan::containers::IsAgentFrame(contents->GetPrimaryMainFrame());
}

bool AgentPageSurfaces::IsShowingHeld(content::WebContents* contents) {
  return contents == showing_;
}

uint64_t AgentPageSurfaces::HoldPicker(
    content::RenderFrameHost* frame,
    HeldPicker picker,
    base::OnceCallback<void(bool show)> show) {
  content::WebContents* contents =
      content::WebContents::FromRenderFrameHost(frame);
  auto [replay, dismiss] = base::SplitOnceCallback(std::move(show));
  PromptQueue::Actions actions;
  actions.replay = base::BindOnce(
      [](AgentPageSurfaces* surfaces,
         base::WeakPtr<content::WebContents> contents,
         base::OnceCallback<void(bool)> show) {
        if (!contents) {
          std::move(show).Run(false);
          return;
        }
        base::AutoReset<raw_ptr<content::WebContents>> showing(
            &surfaces->showing_, contents.get());
        std::move(show).Run(true);
      },
      base::Unretained(this), contents->GetWeakPtr(), std::move(replay));
  // Later, never inside the hook that asked (a refused picker is dismissed at
  // once, while its requester is still being made).
  actions.dismiss = base::BindOnce(
      [](base::OnceCallback<void(bool)> show) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindOnce(std::move(show), false));
      },
      std::move(dismiss));
  const uint64_t id =
      Hold(contents, SurfaceFor(picker), std::string(), std::move(actions));
  if (id) {
    pickers_[id] = frame->GetWeakDocumentPtr();
  }
  return id;
}

void AgentPageSurfaces::ForgetPicker(uint64_t id) {
  pickers_.erase(id);
  opening_.erase(id);
  Forget(id);
}

uint64_t AgentPageSurfaces::Hold(content::WebContents* contents,
                                 PageSurface surface,
                                 std::string detail,
                                 PromptQueue::Actions actions) {
  const uintptr_t tab = KeyFor(contents);
  auto [dismiss_held, dismiss_refused] =
      base::SplitOnceCallback(std::move(actions.dismiss));
  actions.dismiss = std::move(dismiss_held);
  const uint64_t id = queue_.Add(tab, surface, HostOf(contents),
                                 std::move(detail), std::move(actions));
  if (!id) {
    // One focus request is enough; a full tab answers the rest as cancelled.
    std::move(dismiss_refused).Run();
    return 0;
  }
  if (!tabs_.contains(tab)) {
    tabs_[tab] = std::make_unique<TabWatch>(this, contents);
  }
  Notify(tab, surface, /*held=*/true);
  return id;
}

void AgentPageSurfaces::Forget(uint64_t id) {
  const UserPrompt* prompt = queue_.Find(id);
  if (!prompt) {
    return;
  }
  const uintptr_t tab = prompt->tab;
  const PageSurface surface = prompt->surface;
  queue_.Forget(id);
  Notify(tab, surface, /*held=*/false);
}

std::vector<uint64_t> AgentPageSurfaces::DialogsOf(
    content::WebContents* contents) const {
  std::vector<uint64_t> ids;
  for (const UserPrompt& prompt : queue_.List()) {
    if (prompt.tab == KeyFor(contents) &&
        (prompt.surface == PageSurface::kJavaScriptDialog ||
         prompt.surface == PageSurface::kBeforeUnload)) {
      ids.push_back(prompt.id);
    }
  }
  return ids;
}

content::WebContents* AgentPageSurfaces::ContentsOf(uintptr_t tab) const {
  auto it = tabs_.find(tab);
  return it == tabs_.end() ? nullptr : it->second->web_contents();
}

void AgentPageSurfaces::OnTabDestroyed(uintptr_t tab) {
  queue_.ForgetTab(tab);
  tabs_.erase(tab);
  std::erase_if(pickers_, [this](const auto& picker) {
    return !queue_.Find(picker.first);
  });
  std::erase_if(opening_, [this](uint64_t id) { return !queue_.Find(id); });
  if (on_changed_) {
    on_changed_.Run();
  }
}

void AgentPageSurfaces::OnTabVisible(uintptr_t tab) {
  std::vector<uint64_t> ids;
  for (uint64_t id : opening_) {
    const UserPrompt* prompt = queue_.Find(id);
    if (prompt && prompt->tab == tab) {
      ids.push_back(id);
    }
  }
  for (uint64_t id : ids) {
    Open(id);
  }
}

void AgentPageSurfaces::SweepPickers(uintptr_t tab) {
  std::vector<uint64_t> gone;
  for (const auto& [id, document] : pickers_) {
    const UserPrompt* prompt = queue_.Find(id);
    if (!prompt ||
        (prompt->tab == tab && !document.AsRenderFrameHostIfValid())) {
      gone.push_back(id);
    }
  }
  // Answered as cancelled: the request may still be waiting for it.
  for (uint64_t id : gone) {
    Dismiss(id);
    pickers_.erase(id);
  }
}

void AgentPageSurfaces::Notify(uintptr_t tab, PageSurface surface, bool held) {
  if (on_event_) {
    if (content::WebContents* contents = ContentsOf(tab)) {
      on_event_.Run(contents, surface, held);
    }
  }
  if (on_changed_) {
    on_changed_.Run();
  }
}

}  // namespace koan::agent
