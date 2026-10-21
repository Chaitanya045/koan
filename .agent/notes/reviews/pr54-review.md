# Review: PR #54, Glance and split view like Zen's (Fixes #37)

Branch `feat/zen-glance-split` at `3ca8a45`, diffed against `origin/develop` (`9b1c6d1`). I reviewed the code only; nothing was built or run.

**What I checked it against:**
- Chromium 158 (`~/work/koan/chromium/src`, `git show HEAD:<path>`).
- Zen 1.23.3b's source in `.cache/zen-src`.
- Issue #37 and the PR description.
- The agent brief.

**Hook patches:**
- I applied every changed or new patch with `git apply --check` to pristine copies of `chrome/browser/BUILD.gn`, `chrome_content_browser_client.cc`, `renderer_context_menu/{BUILD.gn,render_view_context_menu.cc}`, `ui/tabs/{BUILD.gn,tab_menu_model.cc,tab_strip_model.cc}` and `blink/.../navigation_policy.cc`. All of them apply cleanly.
- The hooks are small and sit where the REBASE.md table says they do.
- The Linux build log for this branch (`.cache/agent-build-feat-zen-glance-split.log`) ends in a successful build.

**The Mac:** it couldn't be reached over SSH: `chaitanyas-macbook-air` timed out and `192.168.0.104` gave "no route to host". The Mac-specific checks below were done against the Mac sources in the shared Chromium 158 tree instead.

Paths are relative to the worktree. "Verified" means traced through the upstream code; nothing was reproduced at runtime.

---

## Blockers

### B1. Right-clicking a link in a WebContents that isn't a tab (the Glance page itself, DevTools) crashes the browser

- **Where:**
  - `src/browser/ui/views/koan_glance_controller.cc:136-148` (`GlanceControllerFor`).
  - Called from `:431` (`AddZenLinkContextMenuItems`), which the new hook in `render_view_context_menu.cc` (`AppendLinkItems`) runs for every link context menu.
- **What's wrong:**
  - `GlanceControllerFor()` passes the result of `GlobalBrowserCollection::FindBrowserWithTab(contents)` straight to `BrowserView::GetBrowserViewForBrowser()`.
  - `FindBrowserWithTab` returns `nullptr` for any WebContents that isn't a tab (`browser_collection.cc:114-125`: no `TabInterface`, so null).
  - `GetBrowserViewForBrowser()` starts with `browser->GetWindow()` (`browser_view.cc:1229`), so a null browser is dereferenced.
  - The only guard before that is `IsGlanceEnabled()`, which is on by default.
- **Failure scenario:**
  1. Alt+click a link. Glance opens.
  2. Right-click any http(s) link in the Glance card. Its WebContents is not a tab.
  3. `AppendLinkItems` → `koan::AddZenLinkContextMenuItems` → `GlanceControllerFor(glance contents)` → null-pointer crash.

  The same happens on http(s) links in docked or undocked DevTools, extension popups, side-panel WebUIs, and so on. Glance pages are full of links, so users will hit this constantly. No e2e test right-clicks inside the Glance page.
- **Fix:**
  - Null-check the browser: `auto* browser = FindBrowserWithTab(contents); if (!browser) return nullptr;`.
  - Optional, but closer to Zen: let "Open Link in Glance" work inside a Glance page too, which would replace the card's page. To do that, map the Glance WebContents back to its controller, for example with a `WebContentsUserData` set in `Open()`.
  - Add an e2e step that right-clicks a link in the Glance page.

### B2. Expanding or splitting a Glance during its 350 ms grow leaves the page in the new tab permanently shrunk to the grow-start rect

- **Where:**
  - `koan_glance_controller.cc:565-571` (`TakeContents`), used by `ExpandToTab` (`:573`) and `OpenInSplit` (`:590`).
  - The animation it leaves running: `:270-279`.
- **What's wrong:**
  1. The grow animation runs on the page's own `aura::Window` layer: the transform goes from the grow rect to identity, and the opacity from 0 to 1.
  2. `TakeContents()` destroys the widget and hands the WebContents to the tab strip without finishing those animations.
  3. Transform animations are threaded. Until they end, the main-thread value is the start value (`ThreadedTransformTransition::OnStart`, `layer_animation_element.cc:576-579`). That start value is the 10% grow rect.
  4. When the tab's `ContentsWebView` attaches the page, `NativeViewHostAura::AttachNativeView` records `original_transform_ = native_view()->transform()` (`native_view_host_aura.cc:86`). It records that small transform.
  5. From then on, every `ShowWidget()` re-applies `original_transform_` (`:250-260`).
  6. When the tab is detached later, `NativeViewDetaching` writes it back (`:135-137`), so the small transform follows the WebContents around.
- **Failure scenario:**
  1. Alt+click a link.
  2. Within 350 ms, press Ctrl+O (⌘O). The Expand-button click and Split Glance do the same.
  3. The tab opens. As soon as anything lays the contents out again (a window resize, toggling the sidebar or compact mode, opening the find bar or DevTools, switching away and back), the page is drawn as a ~10% card at the click point. Input is mapped through the same transform.
  4. Nothing resets it for the lifetime of that WebContents.

  The `Layout()` guard from #28 doesn't help here: the Glance view is destroyed, not laid out.
- **Fix:**
  - Before `DestroyWidget()` in `TakeContents()`, finish the page's animations so the transform and opacity land on identity and 1. For example, give `KoanGlanceView` a `FinishAnimations()` that runs `if (auto* page = Page()) page->layer()->GetAnimator()->StopAnimating();`.
  - If a Glance can ever be taken mid-close, also force `SetTransform(gfx::Transform())` and `SetOpacity(1)`.
  - Add an e2e step: Alt+click, Ctrl+O within about 100 ms, resize the window, then check that the page still fills its contents view.

---

## Should fix

### S1. With a non-default trigger method, any activation click Glance turns down opens a popup window

- **Where:**
  - `patches/third_party/blink/renderer/core/loader/navigation_policy.cc.patch` (the hook now runs first, `button == 0 && IsGlanceActivationClick(...)` → `kNavigationPolicyNewPopup`).
  - `koan_glance_controller.cc:393-419` (`MaybeOpenGlance`).
  - The existing hook in `browser_web_contents_delegate.cc`.
- **What's wrong:**
  - Blink now rewrites *every* single-modifier link activation for the chosen modifier to `NEW_POPUP`, before it knows the URL or the window.
  - `MaybeOpenGlance` then declines anything that isn't an http(s) link in the active tab of a normal window. Upstream gets a renderer-initiated `NEW_POPUP` and opens a popup window.
  - With Alt (the default) this already happened on develop. Ctrl, Shift and Meta are new in this PR, and those modifiers mean "new tab" or "new window" to users.
- **Failure scenario:** with the trigger set to Ctrl + Click (or Meta on macOS):
  - Ctrl+click a `chrome://`, `chrome-extension://`, `file://` or `mailto:` link: you get a bare popup window, not a background tab.
  - Ctrl+click any link in an installed-app (PWA) window or a popup window: you also get a popup window. `GlanceControllerFor` requires `GetIsNormalType()`.

  `glance_activation.mjs` only Ctrl+clicks an http link, so none of this is tested.
- **Fix:** when the params match the activation-click shape but Glance declines, rewrite the disposition to what the modifier normally means. Read the method from the pref:
  - ctrl, or meta on macOS → `NEW_BACKGROUND_TAB`
  - shift → `NEW_WINDOW`
  - alt → `CURRENT_TAB` (the download can't be recovered, which is the existing behaviour)

  This needs the hook to pass a mutable copy of the params, or `MaybeOpenGlance` to return the fixed-up params. Add a test for Ctrl+click on a `file://` link and on a link in an app window.

### S2. Esc (or a click on the backdrop) closes Glance immediately, even while the user is typing; beforeunload never runs

- **Where:** `koan_glance_controller.cc:732-744` (`HandleKeyboardEvent`), `:531-563` (`Close`/`TearDown`).
- **What's wrong:** Zen protects the page in two ways:
  - It runs `permitUnload()` before closing (`ZenGlanceManager.mjs:1023`, `#checkPermitUnload`).
  - When an element in the Glance page has focus, the first Esc only turns the close button into a red "Confirm" for 3 s (`#handleConfirmationTimeout`, `:1090-1107`; `ZenGlanceChild` sends `hasFocused`). A second Esc closes.

  Koan does neither. It also deletes the WebContents with `DeleteSoon`, so `beforeunload` handlers never run.
- **Failure scenario:**
  1. Glance a page with a comment form and type a paragraph.
  2. Press Esc to dismiss an autocomplete dropdown that the page doesn't `preventDefault`, or click the dimmed page by accident.
  3. The text is gone, with no prompt.
- **Fix:**
  - Run `contents_->DispatchBeforeUnload()`, or close through `WebContents::ClosePage()` with `CloseContents` as the continuation, before `Close()`.
  - For Esc, use Zen's two-step confirm when the focused element is editable. Track focus with `WebContentsObserver::OnFocusChangedInPage` or `FocusedNodeChanged`, show the "Confirm" state on the close button, and close on a second Esc within 3 s.

### S3. macOS: replacing the card's page builds an `AnimationBuilder` with callbacks but no sequence (DCHECK)

- **Where:** `koan_glance_controller.cc:258-291` (`PlayOpenAnimation`), reached with `fresh=false` from `Open()` (`:487-494`).
- **What's wrong:**
  - `OnEnded`/`OnAborted` are registered before checking for a page.
  - On macOS `Page()` is always null (`:355-362`). So when `fresh == false` (an activation click in the Glance page), no `Once()` block is ever added.
  - `~AnimationBuilder` then hits `DCHECK(!next_animation_observer_) << "Callbacks were scheduled without creating a sequence block..."` (`animation_builder.cc`).
  - `build/koan-mac.gn` has `dcheck_always_on = false`, so the product build only drops the callbacks. A Mac debug or DCHECK build crashes on every in-Glance link replacement.
  - On Linux the same path is reached if `Page()` is ever null.
- **Fix:** only register `OnEnded`/`OnAborted` when a sequence will follow. Simplest: `if (!page && !fresh) { ShowShadow(); return; }` before creating the builder.

### S4. macOS: on close the card stays full-size and opaque after the backdrop has faded, then vanishes

- **Where:** `koan_glance_controller.cc:295-313` (`PlayCloseAnimation`), `:531-550` (`Close`).
- **What's wrong:**
  - On macOS the page is an NSView above the compositor, so neither its transform nor its opacity is animated (`Page()` is null).
  - Close fades the backdrop (233 ms) and the buttons (200 ms). The page stays fully drawn until `TearDown()` runs 350 ms later.
  - Opening has the mirror problem: the card appears at full size at once while the backdrop fades in.
- **Failure scenario:** press Esc on the Mac. For about 120 ms an undimmed page has a full-size, button-less card floating over it. Then the card pops away. The overlay also swallows clicks for those 350 ms.
- **Fix:** on non-Aura platforms, either:
  - fade the whole overlay with `widget_->SetOpacity()` (NSWindow alpha includes the hosted NSView), or
  - tear down as soon as the backdrop fade ends, and skip the 350 ms wait.

  The PR description already lists the Mac animation as unverified. This makes the close visibly worse than develop's instant close.

### S5. A trigger-method change only reaches new renderer processes; already-open tabs and same-site new tabs keep the old method

- **Where:**
  - `src/browser/koan_renderer_switches.cc:17-28`.
  - `src/browser/koan_features.cc:34-52`.
  - The settings row in `src/browser/ui/webui/koan_settings_rows.cc:77-82`.
- **What's wrong:**
  - The method is copied onto the renderer command line at process launch.
  - Every open tab keeps the old method. So do new tabs for sites whose process is reused (process-per-site reuse, the spare renderer). The e2e test has to open a throwaway cross-site page to dodge this.
  - Zen applies the pref live.
  - The settings row has no hint about this; only USAGE.md and ZEN_PARITY mention it.
- **Failure scenario:**
  1. Switch to "Ctrl + Click" in settings.
  2. Go back to any open tab and Ctrl+click a link: it opens a background tab, not Glance.
  3. Alt+click still opens Glance there.

  The user concludes the setting is broken.
- **Fix:**
  - Preferably make it live: add a field to `blink::RendererPreferences` and push it with `WebContents::SyncRendererPrefs()` from a pref observer, or use a small Koan renderer mojo interface.
  - Failing that, put a sublabel on the settings row ("Applies to pages opened after the change; restart to apply everywhere"). ZEN_PARITY should then say "Different", not "Same".

### S6. "Glance on/off" from the issue's checklist is not a setting; ZEN_PARITY calls it "Same"

- **Where:** `koan_settings_rows.cc:77-82`, `docs/ZEN_PARITY.md:67`.
- **What's wrong:**
  - Issue #37's first checklist item is "Trigger setting … , Glance on/off".
  - Zen has an "Enable Glance" checkbox (`zen.glance.enabled`, `zen-glance-enabled` in `zen-preferences.ftl:25`) in the same group.
  - Koan only has the `koan-glance` feature in chrome://flags, which needs a restart and is hidden from normal users.
  - The parity table still marks the row "Same".
- **Fix:**
  - Add a `koan.glance.enabled` boolean pref with an "Enable Glance" checkbox row in the glance group.
  - Gate `IsGlanceActivationClick` (pass it as a renderer switch alongside the method), `MaybeOpenGlance` and the context-menu item on it.
  - Or mark the row "Different" with the reason.

### S7. (Uncertain) macOS default trigger modifier

- **Where:** `src/browser/koan_prefs.cc:18` (`"alt"` on every platform), `docs/USAGE.md:128`.
- **What's wrong (maybe):**
  - The review brief says Zen's macOS default is ⌘. Koan registers `alt` everywhere, so ⌥+click on the Mac.
  - `.cache/zen-src` is extracted from Zen's Linux omni.ja. Its prefs are preprocessed per platform, so a Mac-specific default can't be confirmed or ruled out from this tree.
  - Pointing the other way: `zen-default-prefs.js:13` says `alt`, while the fallbacks in `ZenGlanceChild.sys.mjs:22-24` and `zen-settings.js:1186-1189` say `ctrl`.
- **If Zen's Mac default is meta:** use `#if BUILDFLAG(IS_MAC) "meta" #else "alt"` in `RegisterProfilePrefs`.
  - Note that the blink hook runs before upstream's `meta` → new-background-tab branch (`navigation_policy.cc`, `IS_MAC`). ⌘+click would then stop opening background tabs on the Mac.
  - Document that in USAGE.md. Please confirm against a Mac Zen build first.

### S8. The e2e tests don't pin the feature down well enough

- **Where:** `scripts/e2e/glance_split.mjs:227-238, 273-287`, `scripts/e2e/glance_activation.mjs`.
- **What's wrong:**
  - Menu items are picked by position: `section.at(-1)`, `section.at(-3)`, `items[1]`. Their labels are never checked, although `sidebar_tabs.mjs` already reads view `Text` through UI DevTools.
  - So the test never checks the strings it was written for: "Open Link in Glance", "Split Link to New Tab", "Split Out Tab", "Add Split View...", "Join 2 Tabs". If upstream reorders the menu, a different item is clicked and the test can still pass.
  - Not covered at all:
    - a right-click inside the Glance page (B1);
    - expanding or splitting during the grow (B2);
    - the grow and shrink animation itself (screenshots only);
    - Ctrl+Alt+G with three tabs selected (the limit toast from the shortcut path);
    - the "Join N Tabs" label;
    - Glance dropping on a tab switch;
    - an activation click on a non-http link with a non-default method (S1).
- **Fix:**
  - Find menu items by label.
  - Add the cases above. B1 and B2 would each have been caught by a single step.

### S9. Shared toast: this PR reuses #50's toast, but #36's branch adds a second `KoanToast` with the same name and files

- **Where:**
  - `src/browser/ui/views/koan_toast.{h,cc}`.
  - `koan_browser_view_controller.{h,cc}` (`toast()`).
  - `feat/zen-urlbar`'s `src/browser/ui/views/koan_toast.{h,cc}`.
- **What's right:**
  - This PR does not add a second toast. It moves #50's toast out of `KoanSidebarTabsController` into a per-window `KoanToast`. "Tabs Closed!" and the split-limit toast both use it, and `koan_sidebar_tabs.cc` drops its copy.
- **What's wrong:**
  - `feat/zen-urlbar` (#36) adds files with the same path and class name, a different API, and the same `toast()` accessor. Its API has no `announcement` argument and adds `OnBrowserViewLayout()` to keep the toast in its corner.
  - Whichever lands second gets an add/add conflict.
  - This version doesn't follow window resizes. `Show()` positions the toast once (`koan_toast.cc:236-239`), so maximising or resizing during its 2 s leaves it mid-window or clipped.
- **Fix:**
  - Agree on one API now: this PR's `announcement` parameter plus #36's `OnBrowserViewLayout()`, called from `KoanBrowserViewController`'s layout hook.
  - Land it first, or rebase #36 onto it.

---

## Nits

### N1. The card's shadow flashes on during close and replace when an open animation is aborted

- **Where:** `koan_glance_controller.cc:265-269` (`OnEnded`/`OnAborted` → `ShowShadow`), `:295-297`.
- **What's wrong:** `OnAborted` runs as soon as a new animation preempts the open one, for example Esc during the grow, or a replace during a grow. That happens after `PlayCloseAnimation` (or the next `PlayOpenAnimation`) has hidden the shadow. The full-size card shadow is then drawn around a shrinking card while the backdrop fades.
- **Fix:** bind `OnAborted` to a no-op. Or keep a "closing" flag on the view that `ShowShadow` checks.

### N2. A stale delayed `TearDown` can cut a later close short

- **Where:** `koan_glance_controller.cc:540-549`.
- **Scenario:**
  1. Close (task A is posted for +350 ms).
  2. Within the 350 ms, an activation click in the still-visible Glance page re-opens it.
  3. Close again at +300 ms.
  4. Task A fires at +350 ms, sees `closing_ == true` and tears down 50 ms into the second shrink.
- **Fix:** use a generation counter or a `base::OneShotTimer`, or tear down from the close animation's `OnEnded`.

### N3. Keyboard edge cases

- **Where:** `koan_shortcuts.cc:211-216`, `koan_glance_controller.cc:338-346`.
- **Ctrl/⌘+O:** `is_open()` is still true during the 350 ms close, so the key is consumed and the posted `ExpandToTab` is a no-op. The key is swallowed.
- **Esc:** it only works through `HandleKeyboardEvent` of the Glance WebContents. With focus on one of the Glance buttons (Tab from the page), Esc does nothing.
- **Fix:**
  - Return false from `kGlanceExpand` while closing.
  - Handle `VKEY_ESCAPE` in `KoanGlanceView` (an accelerator or `OnKeyPressed`).

### N4. The overlay has no accessible role or name

- **Where:** `koan_glance_controller.cc:220-250`.
- **What's wrong:** screen readers get a WebView plus three buttons, with no container announcing "Glance".
- **Fix:** set `GetViewAccessibility().SetRole(ax::mojom::Role::kDialog)` and a name ("Glance: <title>") on `KoanGlanceView` or on the card.

### N5. The grow origin is wrong for clicks inside cross-site iframes

- **Where:** `koan_glance_controller.cc:449-462`.
- **What's wrong:** `DidGetUserInteraction` is fired for OOPIF widgets too (`HasMatchingWidgetHost`), and `PositionInWidget()` is then relative to the iframe, not the page. The card grows from the wrong place.
- **Fix:** ignore events whose widget isn't the main frame's. `RenderWidgetHost` isn't passed in, so compare against `web_contents()->GetRenderWidgetHostView()` in a `RenderWidgetHost::InputEventObserver`. Or accept this and document it.

### N6. "Open Link in Glance" from the context menu loads with no referrer and no initiator

- **Where:** `koan_glance_controller.cc:436-444`.
- **What's wrong:**
  - Upstream's "Open link in new tab" passes the frame's referrer (subject to referrer policy) and the frame origin as initiator.
  - Koan's version passes `content::Referrer()` and is browser-initiated with no initiator.
  - So it behaves like an omnibox load: no `Referer` header, and SameSite=Strict cookies are sent.
- **Fix:** pass `params_.page_url`/`frame_url` with `params_.referrer_policy`, and the initiator origin. That means forwarding them from the hook, for example with `koan::OpenLinkInGlance(source_web_contents_, params_)`.

### N7. "Join N Tabs" past two tabs is shown disabled, while Zen shows the limit toast

- **Where:** `src/browser/ui/views/koan_split_view.cc:76-82`.
- **What's wrong:** keyboard and Glance paths show "Can't add more panels…". The tab menu greys out "Join 3 Tabs" with no explanation.
- **Fix:** consider keeping it enabled, intercepting `CommandAddToSplit` for N > 2 in the existing tab_strip_model hook, and showing the toast. Optional.

### N8. The easing isn't Zen's, but the docs say it is

- **Where:** `koan_glance_controller.cc:101-109`, `docs/ZEN_PARITY.md:69`, PR text ("Zen's eased curve").
- **What's wrong:** for the open, Zen runs `easeOutBack(c1 = 0.4)` (an overshoot) inside `ease-in-out`. For the close, it runs `1-(1-t)^6` inside `ease-out`. Koan uses `ACCEL_40_DECEL_100_3` (cubic-bezier(0.4, 0, 0, 0.97), no overshoot) and `ACCEL_LIN_DECEL_100`. Zen's background also uses a spring with bounce 0.2.
- **Fix:** close enough, but say "similar easing" in the docs, or build the overshoot with a custom `gfx::Tween` or a two-step sequence.

### N9. Colour IDs for #48

- **Where:** `koan_glance_controller.cc:98-99, 181, 371, 376`.
- **What's wrong:** the rule is technically met, since every colour comes from an ID. But Glance uses upstream IDs directly: `kColorToolbar`, `kColorShadowValueAmbientShadowElevationFour`, `kColorSysInverseSurface` and `kColorSysInversePrimary`. The toast, by contrast, uses `kColorKoanToast*`. The shadcn theme (#48) can't restyle Glance without recolouring all of Chromium's UI that uses those IDs.
- **Fix:** add `kColorKoanGlanceBackdrop`, `…ButtonBackground`, `…ButtonIcon` and `…CardShadow` to `koan_color_id.h`, mapped in `koan_color_mixer.cc` to today's values.

### N10. macOS key and click details (code-read against Chromium's Mac sources; not run)

- **Ctrl + Click can never trigger Glance on macOS.** Blink turns Ctrl+left-click into a context menu and never dispatches `click` (`mouse_event_manager.cc:343-350`, `IS_MAC`). The option is dead there: hide it on macOS or add a note.
- **The new Accel+Alt split shortcuts override Mac defaults.** ⌘⌥U overrides Chrome's View Source (`accelerators_cocoa.mm:57`), and ⌘⌥H overrides AppKit's "Hide Others" (`main_menu_builder.mm:84-87`). Koan's normal-priority FocusManager accelerators win in `postPerformKeyEquivalent` before the main menu. This is probably Zen's behaviour too, but it should be listed in USAGE.md's "Replaces" column.
- **New Empty Split View is still Ctrl+Shift+*** (`koan_shortcut_table.cc:128-129`), while Zen's is `accel+shift` (`ZenKeyboardShortcuts.mjs:1167`). That makes it inconsistent with the other split keys this PR moved to Accel.

### N11. `IsGlanceActivationClick` re-parses the command line on every link activation; the switch name lives in the features header

- **Where:** `src/browser/koan_features.cc:34-52`, `koan_features.h:26-28`.
- **Fix:** cache the parsed method in a function-local `static const`. Move `kGlanceActivationMethodSwitch` to `koan_renderer_switches.h`, or a `koan_switches.h` with no dependencies.

### N12. 3001 is cast to `tabs::TabContextMenuCommand`, which has no enumerator anywhere near it

- **Where:** `koan_split_view.h:32`, the casts in `tab_context_menu_controller.cc:65-119`.
- **What's wrong:** `TabContextMenuCommand` is an unscoped enum without a fixed underlying type, and its values run 0 to 30. `static_cast` of 3001 into it is unspecified or undefined in principle, and `-fsanitize=enum` would flag it. It works in practice.
- **Fix:** handle Koan's ID in a `TabContextMenuController`-level hook, before the cast. Or leave a comment that this is a known, accepted wart.

### N13. Patch-file churn and conflicts with other branches

- **Format churn.** Four existing patches lose their `diff --git` and `index` lines and their hunk function context: `chrome/browser/BUILD.gn.patch`, `ui/tabs/BUILD.gn.patch`, `ui/tabs/tab_strip_model.cc.patch` and `navigation_policy.cc.patch`. They still apply. But `scripts/apply_patches.sh export` writes `git diff --full-index` format, so the next export flips them back. Mixed formats make patch diffs noisy.
- **Conflicts.** `patches/chrome/browser/ui/tabs/tab_menu_model.cc.patch` is also created, with different content, by `feat/zen-spaces` and `feat/zen-pinned-essentials`. The shared checkout already has the Spaces "Move To" hook applied. Expect an add/add conflict; coordinate one combined patch.
