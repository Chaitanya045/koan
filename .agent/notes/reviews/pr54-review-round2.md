# Review, round 2: PR #54, Glance and split view (#37)

Branch `feat/zen-glance-split` at `af7a373`, after `git fetch origin`, diffed against `origin/develop`. Code read only: nothing was built or run, because the shared build lock is in use.

**What I checked it against:**
- Pristine Chromium 158 (`~/work/koan/chromium/src`, `git show HEAD:<path>`).
- Round 1 (`pr54-review.md`) and the author's per-finding reply.
- The Mac could not be reached: `chaitanyas-macbook-air` timed out and `192.168.0.104` gave "no route to host". The Mac checks below were done against the Mac sources in the shared Chromium 158 tree.

**The author's test run:** it was on `3cd47a2`. The only difference from `af7a373` is the order of two `#include` lines in `koan_settings_rows.cc`, so the passing Linux build and e2e run still stand for this head. No Mac build has been done.

Paths are relative to the worktree, and `koan_glance_controller.cc` means `src/browser/ui/views/koan_glance_controller.cc`.

## What the round-1 fixes look like now

**B1 is fixed.**
- `GlanceControllerFor()` returns null when `FindBrowserWithTab()` does (`koan_glance_controller.cc:183-187`).
- **The `GlancePage` marker:**
  - `TakeContents()` removes it (`:753`).
  - `OnBrowserViewDestroying()` deletes the contents synchronously at the start of `~BrowserView`, so the marker goes with it.
  - After `TearDown()` the marker stays on the contents until its `DeleteSoon`. By then the contents have no delegate and no widget, so nothing can reach the marker.
  - `Browser::TabStripEmpty` posts `CloseFrame`, so a `DeleteSoon` posted by the last tab's removal runs before `~BrowserView`.
  - So the marker can't be used after the controller dies. There is one new problem with the Glance page as a context-menu source; see S1 below.

**B2 is fixed.**
- `ExpandToTab()` and `OpenInSplit()` both reach the tab strip only through `TakeContents()`. Its first steps are `FinishPageAnimations()` (`StopAnimating()`, then identity transform and opacity 1) and `DestroyWidget()` (`:750-751`).
- Both return early while `closing_` is set, and Ctrl/⌘+O passes through while the card shrinks (`koan_shortcuts.cc:214-216`). So no WebContents leaves the card mid-shrink.
- Once identity is set, `NativeViewHostAura`'s detach restores the identity `original_transform_` it recorded when the Glance attached the page.

**S1, the fallback:**
- **No loop:** the fallback disposition is never `NEW_POPUP`, so the recursive `OpenURLFromTab` gets `kNone` from `MaybeOpenGlance`.
- **No new popup-blocker bypass:** the fallback keeps `user_gesture` and `kFromTrustedEvent`, so `ShouldBlockPopup()` lets it through exactly as it would an upstream Shift+click (`popup_blocker.cc`, the "trusted user action" branch).
- **App and popup windows:** they get upstream's background-tab and new-window behaviour.
- **Gaps:** extension popups are not covered (S2 below), and Glance pages bypass the popup blocker in general (S3 below, already on develop).

**S2, beforeunload and dialogs:**
- **The controller's lifetime:** it can't receive callbacks after it dies, because the delegate is nulled first.
- **Closing the window while a dialog is up:** `AppModalDialogController` is a `WebContentsObserver` and invalidates itself in `WebContentsDestroyed()`.
- **Double close:**
  - `Close()` is a no-op while `awaiting_beforeunload_` or `closing_` is set.
  - The `ClosePage()` that content issues after a "Leave" ends in `CloseContents` → `Close()`, which is a no-op because `closing_` is set.
- **Anchoring on Linux:** the dialog is parented to the browser window (the top-level window of the page's `aura::Window`).
- **Anchoring on macOS:** `WebContentsViewMac::GetTopLevelNativeWindow()` returns the Glance overlay's own child NSWindow, so the sheet hangs off the overlay, not the browser window. That is not a crash, but the visuals are unverified.
- **Problems found:** see S4 and N1 below.

**S3 and S4, macOS:**
- The no-page path is right. `PlayOpenAnimation()` returns before creating a builder, `CloseNow()` tears down at once when `!CanAnimate()`, and `FinishPageAnimations()` is a no-op.
- `aura::Window` is only used inside `#if defined(USE_AURA)` (`:470-477`).
- However, the file does not compile on macOS at all (B1 below).

**S6:** `koan.glance.enabled` is applied everywhere:
- the renderer switch (`off`);
- `MaybeOpenGlance` (declined → fallback);
- the context-menu item;
- activation clicks inside the Glance page (`:881`);
- the settings row it depends on.

**The colour IDs:**
- All six `kColorKoanGlance*` IDs are mapped in `AddKoanColors`, which already uses chrome-level IDs (`kColorToolbarText`). `kColorToolbar` therefore resolves.
- No raw colours remain in the views code, apart from `SK_ColorTRANSPARENT` for the shadow fill and the label background.

**The toast:** `KoanToast::OnBrowserViewLayout()` repositions the toast from the root view's width on every `OnViewBoundsChanged`. It is safe with no toast showing and while one fades out, and the corner position is the same as develop's.

**The Ctrl + Click option:** it is hidden on macOS (`koan_settings_rows.cc:73-80`).

**The patches:**
- I copied the pristine originals of all 9 patches this PR changes or adds into a scratch git repo (`/tmp/pr54r2-1oJ7`), and `git apply --check` passes for every one.
- They are all in `--full-index` form with function context. The hooks are one call or one early return each.
- All of them are in docs/REBASE.md: Glance trigger, Renderer switches, Glance activation, Link context menu and Tab context menu, with their `BUILD.gn` patches mentioned.

---

## Blockers

### B1. `koan_glance_controller.cc` doesn't compile on macOS: Aura's `window.h` is included unconditionally, and it has an `#error` for Apple

- **Where:**
  - `src/browser/ui/views/koan_glance_controller.cc:51`: `#include "ui/aura/window.h"`, outside any guard. The guarded copy is at `:92-94`.
  - `ui/aura/window.h:49-51` (Chromium 158): `#if BUILDFLAG(IS_APPLE) #error "This file must not be included on macOS; Chromium Mac doesn't use Aura."`
  - The file is in `//koan/browser/ui/views:impl` sources on every platform (`src/browser/ui/views/BUILD.gn:37`). Only the `//ui/aura` dep is behind `use_aura` (`:124-128`).
- **How it got in:** commit `db1801a`. On develop the include was only inside `#if defined(USE_AURA)`.
- **Failure scenario:** the next Mac build (`build/koan-mac.gn`) stops in `koan_glance_controller.cc` with that `#error`. macOS is the product platform, so the product can't be built from this branch.

  Linux builds fine, which is why the author's run passed. The PR never ran a Mac build, and the S3/S4 fixes have never been compiled for the Mac.
- **Fix:** delete line 51. The guarded include at `:92-94` is the one that's needed. Then do a Mac compile of `//koan/browser/ui/views:impl` before merging; the user's consent is needed for that.

---

## Should fix

### S1. "Open Link in Glance" in a shrinking Glance page makes the dying page the new card's source: use-after-free

- **Where:**
  - `koan_glance_controller.cc:565-582` (`OpenLinkInGlance` passes the menu's `source_web_contents_`, which for a menu inside the card is the Glance page itself, found through the B1 marker at `:179-182`).
  - `:626-641` (`Open()`):
    - `if (closing_) TearDown();` hands the current `contents_` (that same Glance page) to `DeleteSoon`;
    - `source_ = source;` then stores the Glance page as the new card's source.
- **What's wrong:**
  - The activation-click path inside the page passes `source_` (`:882`), which is correct.
  - The context-menu path passes the page itself. While `closing_` is set, `Open()` takes the fresh-card branch with that page as the new card's source.
  - On the next task the page is deleted, and `source_` dangles. Nothing resets it: `OnTabStripModelChanged` only compares it with removed tabs.
- **Failure scenario:**
  1. A page in Glance runs `window.close()` on a timer. Blink allows that because the Glance page's history has one entry, and `CloseContents` (`:915-920`) starts the 350 ms shrink.
  2. Before that, the user right-clicked a link in the card. The context menu stays open while the card shrinks.
  3. The user picks "Open Link in Glance" within the 350 ms.
  4. A new card opens with `source_` = the freed page.
  5. Esc or the close button → `CloseNow()` → `source_->Focus()` (`:693-695`). Or a Ctrl+click in the new card → `source_->GetDelegate()->OpenURLFromTab(...)` (`:892-894`). Either is a use of freed memory in the browser process. With BackupRefPtr it's a crash rather than an exploit.

  This is Linux only: on macOS `CloseNow()` tears down at once, so `closing_` is never observable.
- **Fix:** at the top of `Open()`, map a Glance-page source to the tab under it, before `TearDown()` can clear it:

  ```cpp
  if (source == contents_.get()) source = source_;
  ```

  Add an e2e step: right-click a link in the card, start a close, then run "Open Link in Glance" before the shrink ends.

### S2. Round 1's S1 isn't fixed for extension popups: an activation click there still opens a bare popup window

- **Where:**
  - `koan_glance_controller.cc:527` (`if (!features::IsGlanceEnabled() || !browser || !source) return GlanceClick::kNone;`).
  - The path into it: `ExtensionViewHost::OpenURLFromTab()` allows `NEW_POPUP` and calls `delegate_->OpenURL()` (`extension_view_host.cc`). Then `ExtensionViewHostBrowserDelegate::OpenURL()` → `Browser::OpenURL()` (`extension_view_host_factory.cc:52-57`). That calls `OpenURLFromTab(nullptr, params, ...)` (`browser.cc:546-556`).
- **What's wrong:**
  - The extension's renderer turns the activation click into `NEW_POPUP` like any other renderer does.
  - The browser side sees `source == nullptr`, returns `kNone`, and upstream opens a `NEW_POPUP` window.
  - So the fallback only covers WebContents that are tabs. Popup blocking is skipped for a null source too (`is_popup = source && ...`).
- **Failure scenario:**
  - With the trigger on Meta (⌘ on macOS) or Ctrl (Linux): ⌘-click a link in an extension's toolbar popup (a password manager's or a reading list's), expecting a background tab. A separate popup window opens instead.
  - With the default Alt: Alt+click there also gives a popup window. That was already so on develop.
- **Fix:** in `MaybeOpenGlance`, when `source` is null but the params match the activation-click shape, return `kDeclined` with a fallback read from `browser->GetProfile()`'s prefs. To do that, make `FallbackDisposition` take a `PrefService*`. Add an e2e step if UI DevTools can reach an extension popup; otherwise add a unit test of `MaybeOpenGlance` with a null source.

### S3. Glance pages bypass Chrome's popup blocker: `window.open()` without a user gesture always succeeds (already on develop)

- **Where:**
  - `koan_glance_controller.cc:643-646`: the Glance WebContents is created bare, with no `PopupBlockerTabHelper`. That helper is attached only in `TabFeatures` (`chrome/browser/ui/tabs/tab_features.cc:1446-1455`).
  - `:899-913`: `AddNewContents` forwards every new window to the tab's delegate.
- **What's wrong:**
  1. `ChromeContentBrowserClient::CanCreateWindow()` calls `MaybeBlockPopup(WebContents::FromRenderFrameHost(opener), ...)`, which is the Glance page.
  2. `MaybeBlockPopup()` returns "not blocked" as soon as `PopupBlockerTabHelper::FromWebContents()` is null (`popup_blocker.cc`, "Check |popup_blocker| first").
  3. The popup reaches `KoanGlanceController::AddNewContents`, then `BrowserWebContentsDelegate::AddNewContents`, and opens.
- **Failure scenario:**
  1. Alt+click a link to an ad-heavy site.
  2. Its `setTimeout(() => window.open('https://ad.example/'), 0)` (or a loop of them) opens popup windows or tabs with no gesture. In a normal tab the same page gets the "Pop-ups blocked" icon and nothing opens.
- **Why it's here:** this predates this PR, since develop's Glance had the same forwarding. It is still the answer to the round-2 question "does it let a page open windows without a user gesture?". The PR makes Glance much easier to reach (context-menu item, the trigger setting), and S2's fix in this PR already touches this delegate.
- **Fix, either of these:**
  - In `Open()`, call `blocked_content::PopupBlockerTabHelper::CreateForWebContents(contents_.get())`. Blocked popups then have no omnibox UI, but they are blocked.
  - Or in `AddNewContents`, when `!user_gesture`, set `*was_blocked = true` and drop the contents.

### S4. The Glance page's app-modal dialog state is never reset, so a later Glance page can inherit "Prevent this page from creating additional dialogs", which also auto-approves beforeunload

- **Where:**
  - `koan_glance_controller.cc:947-952`: the dialog manager is `AppModalDialogManager`.
  - `:738` (`TearDown`), `:816` (`OnBrowserViewDestroying`) and `:752` (`TakeContents`) all call `contents_->SetDelegate(nullptr)` before the contents are deleted or moved.
- **What's wrong:**
  - `AppModalDialogManager` keeps `javascript_dialog_extra_data_`, keyed by the `WebContents*`. It holds `has_already_shown_a_dialog_` and `suppress_javascript_messages_`.
  - The only place an entry is erased is `CancelDialogs(web_contents, reset_state=true)` (`app_modal_dialog_manager.cc:272-273`).
  - `~WebContentsImpl` reaches that call only through `CancelDialogManagerDialogs()`, which returns early when `!delegate_` (`web_contents_impl.cc`, `CancelDialogManagerDialogs`).
  - The controller always nulls the delegate first. So every Glance page that showed a dialog leaves an entry keyed by a freed pointer.
  - A later WebContents allocated at the same address (PartitionAlloc reuses freed slots in the same bucket first) inherits that entry. Of those, only a Glance page reads this manager.
- **Failure scenario:**
  1. Glance page A shows two `alert()`s. The user ticks "Prevent this page from creating additional dialogs" and closes the card.
  2. The next Glance page B gets A's address and is a form the user types into.
  3. On Esc, `RunBeforeUnloadDialogWithOptions()` sees `suppress_javascript_messages_` and proceeds without asking ("lost its privilege to deny unloading"). The text is lost, which is exactly what round 1's S2 was about.
  4. Without the tick, B's first dialog shows the suppress checkbox as if B had shown one before.
- **Fix:** before nulling the delegate in `TearDown()`, `OnBrowserViewDestroying()` and `TakeContents()`, call:

  ```cpp
  javascript_dialogs::AppModalDialogManager::GetInstance()->CancelDialogs(contents_.get(), /*reset_state=*/true);
  ```

  In `TearDown()` and `OnBrowserViewDestroying()` this also closes any dialog that is still open, synchronously.

---

## Nits

### N1. Expanding or splitting while beforeunload is in flight: the new tab then closes itself

- **Where:** `koan_glance_controller.cc:760-762` and `:777-779` check only `closing_`. `TakeContents()` just clears `awaiting_beforeunload_` (`:756`).
- **What's wrong:**
  - The page's beforeunload (`TAB_CLOSE`) keeps running.
  - When the reply comes, or when the 500 ms `kUnloadTimeout` simulates a "proceed", `BeforeUnloadFired` goes to the tab's `BrowserWebContentsDelegate`.
  - `UnloadController::BeforeUnloadFired` returns `proceed` when the browser isn't closing, so `proceed_to_fire_unload` is true.
  - `RenderFrameHostManager::BeforeUnloadCompleted` then runs `ClosePage()`, which ends in `WebContentsImpl::Close()` → `CloseContents` → the tab closes.
- **Failure scenario:**
  1. Open a Glance page whose main thread is busy and that has a beforeunload handler.
  2. Press Esc. Nothing visible happens yet.
  3. Change your mind and press Ctrl+O, or click Expand, within ~500 ms.
  4. The page opens as a tab, then that tab disappears.
- **Fix:** add `|| awaiting_beforeunload_` to the early returns in `ExpandToTab()` and `OpenInSplit()`.

### N2. With Alt, a click Glance declines inside the Glance page navigates the tab underneath, not the card

- **Where:**
  - `koan_glance_controller.cc:890-895` forwards a declined `NEW_POPUP` to `source_`'s delegate, with `source_` as the source.
  - `FallbackDisposition()` → `CURRENT_TAB` (`:217`).
  - The delegate hook re-runs it as `CURRENT_TAB` on `source_`.
- **Failure scenario:** Glance is open (default Alt trigger). Alt+click a `blob:` or `mailto:` link inside the card, or any link after "Enable Glance" was unchecked with the card open. The tab behind the card navigates (or launches the mail handler), and the card stays over it. "Opens in place" should mean in the card.
- **Fix:** in `KoanGlanceController::OpenURLFromTab`, when the forwarded click would fall back to `CURRENT_TAB`, load it in `source` (the Glance page) instead. One way is to call `MaybeOpenGlance` first and handle `kDeclined` + `CURRENT_TAB` locally.

### N3. The "editing" state for Esc goes stale after a navigation that swaps the RenderFrameHost

- **Where:** `koan_glance_controller.cc:603-606` (only `OnFocusChangedInPage` updates `editing_`).
- **What's wrong:** `WebContentsImpl::OnFocusedElementChangedInFrame` drops updates from any frame but the currently focused one. So the old document's shutdown notification ("focus → nullptr", `Document::Shutdown`) is dropped once the new RenderFrameHost has committed. Nothing resets `editing_` on navigation.
- **Failure scenario:**
  1. In a Glance page, type in a search box that submits to another site.
  2. On the results page nothing is focused, but the first Esc only turns the close button red ("Press Esc again to close"). A second Esc is needed.
- **Fix:** reset `editing_` in `InputTracker::PrimaryPageChanged()`, or in `DidFinishNavigation` for a committed, cross-document primary main-frame navigation.

### N4. run.sh's `{dump}` substitution mangles paths containing `&`

- **Where:** `scripts/e2e/run.sh:77`: `"${flag//\{dump\}/$KOAN_DUMP_DIR}"`.
- **What's wrong:** bash 5.2+ has `patsub_replacement` on by default (it is on in this host's bash 5.3), so an unquoted `&` in the replacement stands for the matched text.
- **Verified:** with `d=/home/u/R&D/koan/...`, the result was `--ozone-dump-file=/home/u/R{dump}D/koan/...`.
- **Failure scenario:** a checkout under a path with `&` (`KOAN_TEST_DIR` is `$KOAN_ROOT/.test-profiles`). Chrome then writes its frame dumps to the wrong directory, and `glance_split.mjs` fails to find them.
- **Otherwise the substitution is safe:**
  - the flags file is repo-controlled;
  - the array is quoted, so there is no word splitting or globbing;
  - the value is run.sh's own `mktemp` path.
- **Fix:** quote the replacement: `"${flag//\{dump\}/"$KOAN_DUMP_DIR"}"`.
