// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_GLANCE_CONTROLLER_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_GLANCE_CONTROLLER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"

class BrowserView;
class BrowserWindowInterface;
class GURL;

namespace actions {
class ActionItem;
}

namespace koan {

class KoanGlanceView;

#if BUILDFLAG(IS_MAC)
// macOS, koan_glance_controller_mac.mm. The card's window isn't a browser
// window, so no ChromeCommandDispatcherDelegate handles its key
// equivalents; the card's PreHandleKeyboardEvent() does what it would.

// What ReservedCommandForKeyEvent() returns for a key to swallow: a
// repeating ⌘T / ⌘N / ⇧⌘N, as the delegate drops them.
inline constexpr int kDropKeyEvent = -1;
// The browser command that `event` (a key in the Glance page) runs and that
// the browser reserves (IsReservedCommandOrKey: ⌘W, ⌘T, ⌘N, tab switching
// ...), kDropKeyEvent, or 0.
int ReservedCommandForKeyEvent(BrowserWindowInterface* browser,
                               const input::NativeWebKeyboardEvent& event);
// Window > Minimize (⌘M) and Window > Zoom go to the first responder, which
// is in the card's window while it is key: whether `event` is the key
// equivalent of one of them, and if so, does it to `browser`'s window.
bool PerformWindowMenuKey(views::Widget* browser,
                          const input::NativeWebKeyboardEvent& event);
// A key equivalent nothing else took: the menu bar's item for it, as after
// the page (koan_glance_controller.cc ProcessPriorityKey()).
void PerformMainMenuKey(const input::NativeWebKeyboardEvent& event);
// The card's window is a top-level one (Open()): titled "Glance" for
// VoiceOver, kept out of the Window menu.
void PrepareCardWindow(views::Widget* card);

// What the card does with a key equivalent before the page and the menu bar.
enum class CardKeyResult { kUnhandled, kHandled, kDrop };

// A command-dispatcher delegate on the card's window, as browser windows
// have ChromeCommandDispatcherDelegate: the card's key equivalents go to
// `handler` before the page and the menu bar (prePerformKeyEquivalent:), so
// the browser window's high-priority shortcuts, Minimize / Zoom and the
// reserved commands come first wherever the card's focus is. Without it, a
// key the page doesn't take bubbles to the browser window's dispatcher,
// which looks for accelerators on the card's own FocusManager and misses
// Koan's (⌘⌥→ ran Chrome's next tab with Spaces on). Removed when this goes.
class CardKeyDispatcher {
 public:
  using Handler = base::RepeatingCallback<CardKeyResult(
      const input::NativeWebKeyboardEvent& event)>;
  CardKeyDispatcher(views::Widget* card, Handler handler);
  CardKeyDispatcher(const CardKeyDispatcher&) = delete;
  CardKeyDispatcher& operator=(const CardKeyDispatcher&) = delete;
  ~CardKeyDispatcher();

 private:
  struct ObjC;
  std::unique_ptr<ObjC> objc_;
};
#endif

// Zen-style Glance for one window: a link opened as a floating card over
// the current page, with the page dimmed behind it. The card grows out of
// the click that opened it and shrinks back into it when it is closed (Esc,
// the close button, or a click on the dimmed page). Beside the card: expand
// it into a normal tab, or open it in a split with the current tab. An
// activation click on a link in the Glance page replaces the card's page.
//
// The overlay is a child widget covering the contents area, so it has its
// own native window above the page's: the dimmed area and the buttons get
// mouse events, and the page underneath gets none while Glance is open. The
// card hosts a WebContents owned here until it is promoted into a tab.
class KoanGlanceController : public content::WebContentsDelegate,
                             public TabStripModelObserver {
 public:
  explicit KoanGlanceController(BrowserView* browser_view);
  KoanGlanceController(const KoanGlanceController&) = delete;
  KoanGlanceController& operator=(const KoanGlanceController&) = delete;
  ~KoanGlanceController() override;

  bool is_open() const { return contents_ != nullptr; }
  // While the card shrinks away after a close.
  bool is_closing() const { return closing_; }
  content::WebContents* contents() const { return contents_.get(); }

  // Runs `action` (Close, ExpandToTab, OpenInSplit) after the current event
  // is done: they destroy the overlay widget, which must not happen inside
  // one of its own buttons' click handlers.
  void PostAction(void (KoanGlanceController::*action)());

  // Opens `params.url` in Glance, over `source` (the active tab), or loads
  // it in the open card.
  void Open(const content::OpenURLParams& params, content::WebContents* source);
  // Lets the page's beforeunload handlers ask to stay, then shrinks the card
  // back into the click it grew out of and drops it.
  void Close();
  // Esc, in the page or on the buttons. Like Zen, while the page has an
  // editable field focused the first Esc only turns the close button into
  // "Confirm" for 3s; a second one closes.
  void OnEscape();
  void ExpandToTab();
  void OpenInSplit();

  // Browser commands (IDC_*) while the card is open act on the Glance page,
  // as on a tab in Zen: ⌘W closes the card, ⌘R reloads the Glance page, ...
  // Returns whether it took `command_id`; false while no card is open, and
  // while the card is closing for anything but ⌘W. Called from
  // koan_command_policy.cc for every route to a command (keys, the Mac menu
  // bar, chrome::ExecuteCommand, toolbar buttons).
  bool HandleBrowserCommand(int command_id);

  // Gives the Glance page keyboard focus (the URL bar opened over it closed).
  void FocusPage();
  // What the user picked in the URL bar opened over the card (⌘L): loads it
  // in the Glance page. False if there is no open card.
  bool LoadFromUrlBar(const GURL& url,
                      ui::PageTransition transition,
                      const std::string& extra_headers);

  // Called by KoanBrowserViewController.
  void OnBrowserViewLayout();
  void OnBrowserViewDestroying();

  // content::WebContentsDelegate:
  void NavigationStateChanged(content::WebContents* source,
                              content::InvalidateTypes changed_flags) override;
  void LoadingStateChanged(content::WebContents* source,
                           bool should_show_loading_ui) override;
  void ShowRepostFormWarningDialog(content::WebContents* source) override;
  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>
          navigation_handle_callback) override;
  content::WebContents* AddNewContents(
      content::WebContents* source,
      std::unique_ptr<content::WebContents> new_contents,
      const GURL& target_url,
      WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures& window_features,
      bool user_gesture,
      bool* was_blocked) override;
  void CloseContents(content::WebContents* source) override;
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override;
  bool HandleKeyboardEvent(content::WebContents* source,
                           const input::NativeWebKeyboardEvent& event) override;
  void BeforeUnloadFired(content::WebContents* source,
                         bool proceed,
                         bool* proceed_to_fire_unload) override;
  content::JavaScriptDialogManager* GetJavaScriptDialogManager(
      content::WebContents* source) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  // Watches a WebContents for what Glance needs from its input: the last
  // mouse press, in screen coordinates (the card grows out of it; Zen grows
  // it out of the clicked link's rect, which only its content process
  // knows; a key press forgets it, so a Glance opened from the keyboard
  // grows out of the card's centre), and whether an editable field has
  // focus (for Esc).
  class InputTracker : public content::WebContentsObserver {
   public:
    explicit InputTracker(content::WebContents* contents);

    struct Press {
      gfx::Point point;
      base::TimeTicks time;
    };
    const std::optional<Press>& press() const { return press_; }
    bool editing() const { return editing_; }

    // content::WebContentsObserver:
    void DidGetUserInteraction(const blink::WebInputEvent& event) override;
    void PrimaryPageChanged(content::Page& page) override;
    void OnFocusChangedInPage(
        const content::FocusedNodeDetails& details) override;

   private:
    std::optional<Press> press_;
    bool editing_ = false;
  };

  // Watches the tab the card was opened over: when its WebContents goes
  // away (the tab closed; a replace, which only the legacy discard does in
  // M158, tears down at once in OnTabStripModelChanged()), the card goes too.
  class SourceTracker : public content::WebContentsObserver {
   public:
    SourceTracker(KoanGlanceController* controller,
                  content::WebContents* source);

    // content::WebContentsObserver:
    void WebContentsDestroyed() override;

   private:
    const raw_ptr<KoanGlanceController> controller_;
  };

  // Tracks presses in the active tab (also after a navigation gave it a new
  // WebContents).
  void TrackActiveTab();

  // The tab's WebContents the card was opened over; null once it is gone.
  content::WebContents* source() const {
    return source_ ? source_->web_contents() : nullptr;
  }

  // The rect the card grows out of, in the overlay's coordinates: a small
  // card around the latest press in the tab or the Glance page, or in the
  // card's centre.
  gfx::Rect GrowRect() const;

  // Shrinks the card away (after beforeunload let it go) and drops it.
  void CloseNow();
  // After the tab under the card went away (SourceTracker).
  void TearDownIfSourceGone();
  // ⌘L in a Glance: the URL bar opens with the Glance page's address.
  void EditUrl();
  // A key in the Glance page the browser takes (PreHandleKeyboardEvent()),
  // run after the key's dispatch: it may switch tabs, which closes the
  // card's window, whose key handling is on the stack then.
  void RunBrowserCommand(int command_id);
#if BUILDFLAG(IS_MAC)
  // The card window's key equivalents, first (CardKeyDispatcher): the
  // browser window's steps, as PreHandleKeyboardEvent() has them.
  CardKeyResult OnCardKeyEquivalent(const input::NativeWebKeyboardEvent& event);
  // Steps 2 and 3 of PreHandleKeyboardEvent() (Window > Minimize / Zoom,
  // the reserved commands) for a key equivalent: whether they took it.
  bool TakeKeyEquivalent(const input::NativeWebKeyboardEvent& event);
  // Step 1, after the key's dispatch: the browser window's high-priority
  // shortcut, or if it declines, steps 2 and 3, then the menu bar's item.
  void ProcessPriorityKey(const input::NativeWebKeyboardEvent& event);
#endif

  // While the card is open, the enabled state of Back, Forward, Stop, zoom
  // and Find (kHeldCommands) follows the Glance page, as it follows the
  // selected tab in Zen: set for the Glance page, and set again whenever the
  // command controller sets it for the active tab. Released (upstream's
  // values for the active tab again, unless the window is going) when the
  // card closes or becomes a tab.
  void HoldCommands();
  void ReleaseCommands(bool restore);
  // The held commands' state for the Glance page.
  void ApplyCommandState();
  void ReapplyCommandState();
  bool GlanceEnables(int command_id) const;
  void OnHeldCommandChanged(int command_id, actions::ActionItem* item);
  // Ends the close button's "Confirm" state.
  void EndConfirm();
  // Resets the page's dialogs and detaches it from this delegate.
  void ReleaseContents();
  // Drops the overlay and the Glance page: `contents_` is deleted once the
  // current task is done (this can run inside its delegate callbacks).
  void TearDown();
  // Drops the overlay and returns the Glance WebContents (detached from the
  // card, its animations finished) so it can be inserted into the tab strip.
  std::unique_ptr<content::WebContents> TakeContents();
  // `give_back_key`: if the card's window was key (macOS), the browser
  // window becomes key again; not while the window goes.
  void DestroyWidget(bool give_back_key = true);
  void LayoutWidget();

  const raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<views::Widget> widget_;
#if BUILDFLAG(IS_MAC)
  // On the card's window while it is open (Open(), DestroyWidget()).
  std::unique_ptr<CardKeyDispatcher> key_dispatcher_;
#endif
  raw_ptr<KoanGlanceView> view_ = nullptr;
  std::unique_ptr<content::WebContents> contents_;
  std::unique_ptr<SourceTracker> source_;
  // Where the card grew out of (Zen's originalPosition): it shrinks back
  // there on close.
  gfx::Rect grow_rect_;
  // Set while the card shrinks away; `close_timer_` drops it afterwards.
  bool closing_ = false;
  base::OneShotTimer close_timer_;
  // Set while the page's beforeunload handlers run.
  bool awaiting_beforeunload_ = false;
  // Runs while the close button asks to confirm.
  base::OneShotTimer confirm_timer_;

  std::unique_ptr<InputTracker> tab_input_;
  std::unique_ptr<InputTracker> glance_input_;

  // When ⌘W last closed the card: a second ⌘W right after is the card's,
  // not the tab's (HandleBrowserCommand()).
  base::TimeTicks closed_by_close_tab_at_;

  // HoldCommands().
  bool holds_commands_ = false;
  bool applying_commands_ = false;
  bool reapply_pending_ = false;
  std::vector<base::CallbackListSubscription> command_subscriptions_;

  base::WeakPtrFactory<KoanGlanceController> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_GLANCE_CONTROLLER_H_
