// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_PAGE_SURFACES_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_PAGE_SURFACES_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "content/public/browser/weak_document_ptr.h"
#include "koan/browser/agent/koan_agent_prompts.h"
#include "koan/browser/agent/koan_agent_tab_hooks.h"

class HttpAuthCoordinator;

namespace content {
class WebContents;
}

// Design section 9.4 in the browser: what a page asks to show while an agent
// holds its tab is held in a PromptQueue, shown in the agent indicator, and
// shown the way Chrome would when the user opens it (the tab comes forward
// first). The surfaces and the upstream code each reuses:
//  - alert(), confirm(), prompt() and beforeunload (an app-modal alert on the
//    Mac): Koan's JavaScriptDialogManager for the tab, which hands the dialog
//    to javascript_dialogs::TabModalDialogManager on open. The agent can still
//    answer it with Page.handleJavaScriptDialog (DevTools sees it first);
//  - app links: ExternalProtocolHandler::LaunchUrl on open;
//  - HTTP sign-in: an HttpAuthCoordinator of Koan's own on open;
//  - window.print(): the preview the PrintViewManager was about to show;
//  - window.focus(): the tab comes forward on open;
//  - <input type=file>, the device choosers (WebUSB, WebHID, Web Serial, Web
//    Bluetooth) and getDisplayMedia()'s picker: Chrome's own path once the tab
//    is in front and visible (the file chooser checks both).
// When the agent lets go of the tab, what it held is shown as Chrome would
// have (without bringing the tab forward), except the pickers, which are
// cancelled; when the tab closes it goes, and a picker goes with its page.
//
// One per browser process, UI thread. Installed while agent control forwards
// (AgentForwarder); nothing is held without a lease.
namespace koan::agent {

// A held surface, for the indicator.
struct HeldPrompt {
  UserPrompt prompt;
  raw_ptr<content::WebContents> contents = nullptr;
};

class AgentPageSurfaces : public PageSurfaceDelegate {
 public:
  static AgentPageSurfaces& Get();

  AgentPageSurfaces(const AgentPageSurfaces&) = delete;
  AgentPageSurfaces& operator=(const AgentPageSurfaces&) = delete;

  // `on_event` runs when a surface is held (`held`) and when it is no longer
  // (opened, dismissed, or ended on its own); `on_changed` whenever Prompts()
  // may have changed. Installing also makes this the PageSurfaceDelegate;
  // Uninstall() shows everything held and stops holding.
  using EventCallback = base::RepeatingCallback<
      void(content::WebContents* contents, PageSurface surface, bool held)>;
  void Install(EventCallback on_event, base::RepeatingClosure on_changed);
  void Uninstall();

  std::vector<HeldPrompt> Prompts() const;
  // The indicator's buttons.
  void Open(uint64_t id);
  void Dismiss(uint64_t id);
  // The agent let go of `contents`.
  void ReleaseTab(content::WebContents* contents);

  // PageSurfaceDelegate:
  bool IsHeldByAgent(content::WebContents* contents) override;
  content::JavaScriptDialogManager* DialogManager() override;
  void HoldExternalProtocol(content::WebContents* contents,
                            const GURL& url,
                            ui::PageTransition page_transition,
                            bool has_user_gesture,
                            bool is_in_fenced_frame_tree,
                            const std::optional<url::Origin>& origin,
                            content::RenderFrameHost* initiator) override;
  std::unique_ptr<content::LoginDelegate> HoldHttpAuth(
      HttpAuthRequest request) override;
  bool HoldPrint(content::WebContents* contents,
                 base::OnceCallback<void(bool show)> show) override;
  void HoldActivation(content::WebContents* contents) override;
  bool MayHoldPickers(content::WebContents* contents) override;
  bool IsShowingHeld(content::WebContents* contents) override;
  uint64_t HoldPicker(content::RenderFrameHost* frame,
                      HeldPicker picker,
                      base::OnceCallback<void(bool show)> show) override;
  void ForgetPicker(uint64_t id) override;

 private:
  friend class base::NoDestructor<AgentPageSurfaces>;
  class HeldDialogs;
  class HeldLogin;
  class TabWatch;

  AgentPageSurfaces();
  ~AgentPageSurfaces() override;

  // Holds a surface of `contents`; returns its id, or 0 if the queue refused
  // it (its `dismiss` has then run).
  uint64_t Hold(content::WebContents* contents,
                PageSurface surface,
                std::string detail,
                PromptQueue::Actions actions);
  // The surface `id` ended on its own.
  void Forget(uint64_t id);
  // The dialogs held for `contents` (Koan's dialog manager).
  std::vector<uint64_t> DialogsOf(content::WebContents* contents) const;
  content::WebContents* ContentsOf(uintptr_t tab) const;
  void OnTabDestroyed(uintptr_t tab);
  // The tab became visible: the pickers opened while it wasn't are shown.
  void OnTabVisible(uintptr_t tab);
  // A page of the tab went: its held pickers go too.
  void SweepPickers(uintptr_t tab);
  void Notify(uintptr_t tab, PageSurface surface, bool held);

  PromptQueue queue_;
  std::map<uintptr_t, std::unique_ptr<TabWatch>> tabs_;
  std::unique_ptr<HeldDialogs> dialog_manager_;
  std::unique_ptr<HttpAuthCoordinator> http_auth_;
  // The page each held picker is for.
  std::map<uint64_t, content::WeakDocumentPtr> pickers_;
  // Pickers the user opened whose tab isn't visible yet.
  std::set<uint64_t> opening_;
  // The tab whose held print preview or picker is being shown now (not held
  // again).
  raw_ptr<content::WebContents> showing_ = nullptr;
  bool installed_ = false;
  EventCallback on_event_;
  base::RepeatingClosure on_changed_;
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_PAGE_SURFACES_H_
