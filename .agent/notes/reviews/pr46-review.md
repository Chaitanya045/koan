# Review: PR #46, chrome://koan-settings with a shortcut editor (Fixes #39)

Branch `feat/zen-settings` was diffed against `origin/develop` (7 commits, 34 files, +2449/-107).
Upstream references are Chromium 158 at `/home/chaitanya/work/koan/chromium/src` (HEAD) and Zen 1.23.3b at `.cache/zen-src`.

## What was checked and found OK

- **Hook patches.** Both `patches/chrome/browser/ui/webui/{BUILD.gn,chrome_web_ui_configs.cc}.patch` apply cleanly to the pristine HEAD files (`git apply --check` against `git show HEAD:` copies). Each is a one-line hook (one dep, plus one include and one `AddWebUIConfig`). Both are listed in `docs/REBASE.md`.
- **Chromium APIs.** These exist with the signatures the PR uses:
  - `WebUIDataSource::SetRequestFilter`
  - `ShowSingletonTab(BrowserWindowInterface*, const GURL&)`
  - `GetAcceleratorList()` returning `std::vector<AcceleratorMapping>`
  - `Widget::GetTopLevelWidgetForNativeView`
  - `FocusManager::set_shortcut_handling_suspended`
  - `PrefChangeRegistrar::IsObserved`
  - `base::DictValue`/`ListValue` chaining
  - `sendWithPromise`/`addWebUiListener` in `cr.js`

  The GN targets `//chrome/browser/ui:accelerator_table`, `:singleton_tabs` and `//chrome/browser/profiles:profile` exist, and none of them depends back on `webui:configs`.
- **Key capture.** `BrowserView::PreHandleKeyboardEvent` (browser_view.cc:3246) returns `NOT_HANDLED` while the FocusManager's shortcut handling is suspended, so the capture approach works as described.
- **Handler lifetime.** Message callbacks use `Unretained(this)`, which is the standard pattern because the handler is owned by the WebUI. The pref registrar is created in `OnJavascriptAllowed` and reset in `OnJavascriptDisallowed`. `RenderFrameDeleted` and `PrimaryPageChanged` both call `DisallowJavascriptOnAllHandlers`, so a renderer crash or navigation resumes shortcuts. The widget is observed and the observation is reset in `OnWidgetDestroying`. The destructor resumes shortcuts. I found no use-after-free.
- **Controller lifetimes.** `KoanShortcutController` and `KoanWorkspaceController` `RemoveAll()` their registrars in `OnBrowserViewDestroying`, and `~PrefChangeRegistrar` also cleans up.
- **CSP.** The default `URLDataSource` CSP (script-src `chrome://resources 'self'`, trusted types) is compatible with the page. The JS never assigns HTML strings: `h()` uses `createElement`, properties and `append`. The CSS `data:` mask images are allowed because there is no default img-src.
- **`setPref` validation.** The handler only accepts prefs listed in the table, of the registered type, that are user-modifiable (policy is respected), and one of the row's options.
- **`setShortcut` validation.** It checks the id against the table, the key against `IsValidCustomAccelerator`, and conflicts against the effective bindings on the browser side. It does not trust the JS conflict check.
- **Conflict invariant.** Through the UI, no two actions can end up with the same key: I walked through custom → default → custom sequences and they are all refused correctly. Reset clears the pref.
- **Editor behaviour vs Zen.** It matches `gZenCKSSettings._handleKeyDown` in `content/zen-settings.js`: Esc saves, Backspace clears, Tab leaves, the "Unsaved shortcut!" note appears, and conflicts show as "Conflicts with {group} -> {shortcut}". The display format `Ctrl+Win+Alt+⇧+Key` matches `nsKeyShortcutModifiers.toDisplayString`. The group order matches `VALID_SHORTCUT_GROUPS`. The labels match `zen-preferences.ftl` except N3.
- **Formatting.** All changed `.cc`/`.h` files are clang-format clean with Chromium's `.clang-format`.

No blockers found.

---

## Blockers

None.

---

## Should fix

### S1. Ctrl+, opens a second settings tab after the user picks a pane, and the e2e test asserts this bug
- **Where:** `src/browser/ui/views/koan_shortcuts.cc:190-192`, `src/browser/ui/webui/resources/koan_settings.js:396`, `scripts/e2e/settings.mjs:249-254`
- **What's wrong:**
  - `ShowSingletonTab(browser, GURL("chrome://koan-settings/"))` uses the default `NavigateParams::RESPECT` path behaviour.
  - `GetIndexOfExistingTabMatchingURL` (browser_navigator_params_utils.cc:180-190) only clears the ref, so the path must match exactly.
  - `selectPane()` calls `history.replaceState(.., '/looks' | '/tabs' | '/shortcuts')`, which changes the tab's visible URL.
- **Failure scenario:**
  1. Press Ctrl+, and the page opens at `/`.
  2. Click "Keyboard Shortcuts". The URL becomes `chrome://koan-settings/shortcuts`.
  3. Press Ctrl+, again. A **second** settings tab opens instead of switching to the first. Every further pane click plus Ctrl+, adds another tab.

  The e2e check `after === before + 1` passes only because of this bug: the existing settings tab is at `/shortcuts` (or `/looks` with `SHOTS`). It would fail once the bug is fixed.
- **Fix:**
  - Build the params with `GetSingletonTabNavigateParams(browser, url)`, set `params.path_behavior = NavigateParams::IGNORE_AND_NAVIGATE`, and call `Navigate(&params)`. This is what `chrome::ShowSettingsSubPageInTabbedBrowser` does. `ShowSingletonTabOverwritingNTP(browser, url, IGNORE_AND_NAVIGATE)` would also reuse an NTP, which is arguably nicer.
  - Change the e2e test so that Ctrl+, with no settings tab open opens one, and Ctrl+, with one open (on any pane) activates it, so the tab count stays the same.

### S2. In incognito windows, settings no longer "apply at once to every window", and the page shows the wrong state
- **Where:** `src/browser/ui/webui/koan_settings_ui.cc:55-56` (the handler writes `GetOriginalProfile()->GetPrefs()`), together with existing writers such as `koan_compact_mode_controller.cc:190-197`, which write `browser_view_->GetProfile()->GetPrefs()`.
- **What's wrong:**
  - An incognito window's controllers observe and write the **OTR** PrefService.
  - That service is an `OverlayUserPrefStore`. Writes to a non-allowlisted pref (all `koan.*` prefs) go to the in-memory ephemeral store.
  - Once the ephemeral store holds a value, changes to the persistent store are no longer forwarded (`OverlayUserPrefStore::OnPrefValueChanged`: `if (!ephemeral_->GetValue(key)) ReportValueChanged`).
- **Failure scenario:**
  1. Open an incognito window and press Ctrl+S (compact mode on). This writes the OTR overlay.
  2. Open chrome://koan-settings (Ctrl+, from that window opens it in incognito too, with a handler on the original prefs). The checkbox shows compact mode **off**.
  3. Toggling it has no effect on the incognito window, which stays shadowed until it closes.

  This contradicts the PR text and USAGE.md ("Changes apply at once to every window, and the page follows changes made elsewhere"). The root cause existed before this PR, but this PR is what exposes it and documents behaviour that is false.
- **Fix (pick one):**
  - (a) Have Koan's controllers read, write and observe `GetOriginalProfile()->GetPrefs()` for Koan's UI prefs. This is the simplest and keeps Koan entirely in `//koan`.
  - (b) Register `koan.*` UI prefs as persistent in incognito. This needs a hook in `chrome/browser/prefs/pref_service_incognito_allowlist.cc`.

  Also decide whether Ctrl+, from incognito should open the page in the original profile's window, as chrome://settings does via `ShowSingletonTab(profile->GetOriginalProfile(), ...)`.

### S3. `shortcuts::Bindings()` doesn't remove duplicates, so a new default key in a later Koan version collides with a user's custom key
- **Where:** `src/browser/koan_shortcut_table.cc:149-169` (`Bindings`), `src/browser/ui/views/koan_shortcuts.cc` `RegisterAccelerators`/`AcceleratorPressed`
- **What's wrong:**
  - `Bindings()` adds every custom key and then every default of the non-customised actions, without checking that the result is free of duplicates.
  - The browser-side conflict check only holds at the moment a key is set. Upgrades that add defaults bypass it, and the PR explicitly invites follow-up PRs (#24, #36, #37, …) to add shortcuts.
  - Registering one accelerator twice for the same target hits `DCHECK(!Contains(target)) << "Registering the same target multiple times"` (ui/base/accelerators/accelerator_manager.cc:126).
- **Failure scenario:**
  1. A user binds "Toggle Mute" to Ctrl+T. The editor allows it and shows the "Replaces Chromium's shortcut" note.
  2. #24 later adds a Koan default Ctrl+T (floating new tab).
  3. On next start, `bindings_` contains two Ctrl+T entries. With DCHECKs on (e2e and dev builds), this crashes. In release, the new action is silently dead because the first match wins in `AcceleratorPressed`. The editor shows both actions with Ctrl+T, and every attempt to fix either is refused as a "conflict".
- **Fix:** In `Bindings()`, skip a default whose accelerator is already taken by a custom binding. The user's choice wins, and the editor then shows the new action as "Not set", or as custom-none if you prefer. Add a unit-style check (or e2e step) that pre-seeds `koan.shortcuts` with a key that is another action's default.

### S4. "Fixes #39" closes the issue while several checklist items aren't tracked anywhere; ZEN_PARITY overstates parity
- **Where:** PR body, `docs/ZEN_PARITY.md:76-77`, `src/browser/ui/webui/koan_settings_rows.cc:20-38` (the groups)
- **What's wrong:**
  - Issue #39's Tab Management checklist includes "switch to most recently used tab on close", "close tab and switch to owner on Back with no history", and "Ctrl+Tab cycling options". The PR attributes these to "#33 / #34 territory".
  - #33 is the sidebar tab list and #34 is Spaces. Neither mentions these behaviours.
  - The issue asks for settings owned by other issues to be listed as TODO. These have no owner, so merging with `Fixes #39` drops them.
  - ZEN_PARITY marks "Zen settings pages" and "Shortcut editor" as **Same**. Only 3 settings exist, the editor covers Koan's 26 actions and not Zen's full list, and the Collapsed Sidebar card is missing (it's visible in the comparison screenshot).
- **Failure scenario:** After merge, issue #39 auto-closes. Nobody owns MRU-on-close, close-on-back or Ctrl+Tab ordering, and the parity doc reports the area as done.
- **Fix:** Use "Part of #39" (or open a follow-up issue for the three unowned Tab Management behaviours and link it in the PR's TODO table), and change ZEN_PARITY's "Zen settings pages" row to **Partial**.

### S5. The e2e test doesn't check several things the PR claims or changes
- **Where:** `scripts/e2e/settings.mjs:170-178`, `225-235`
- **What's missing:**
  - **Workspace tint.** The only behaviour change in `koan_workspaces.cc` (tint off restores the user colour live, and the new pref observer) is untested. The test only checks that the checkbox's `checked` flips. It never creates a workspace or reads the theme colour. It also re-clicks the box at line 178 without waiting or checking.
  - **Backspace / Reset.** These are only checked through the displayed text. Nothing checks that Ctrl+M stops muting after it is cleared, or that Ctrl+S toggles compact mode again after Reset. The test does this for the rebind case at lines 200-207, so the pattern is available.
  - **`setPref` validation, the main security property, is never exercised.** For example, `chrome.send('setPref', ['x', 'koan.workspaces.tint_applied', 5])` or a wrong-typed value should resolve `false` and leave the pref unchanged.
  - **Browser-side conflict refusal.** It is never reached, because the JS check refuses first. Call `sendWithPromise('setShortcut', 'mute', {key: 83, modifiers: 2})` directly and expect `false`.
  - The "Unsaved shortcut!" note and the Tab key are not tested.
- **Fix:** Add the checks above. For the tint, create a coloured workspace (as `workspaces.mjs` does), toggle the row, and compare `ThemeService` state through the frame colour in the UI DevTools tree or a screenshot pixel.

---

## Nits

### N1. Setting "New Empty Split View" back to its default key silently drops the keypad key
- **Where:** `koan_settings_handler.cc:198-204`
- **What's wrong:** The action has two defaults (Ctrl+Shift+8 and Ctrl+Shift+Num*). Re-typing Ctrl+⇧+8 compares a one-key list against the two-key default list, so it is stored as custom.
- **Effect:** Ctrl+Shift+Num* stops working.
- **Fix:** Treat any key that is in the action's defaults as "back to default" (`update->Remove`).

### N2. When the browser refuses a key, the field keeps showing the refused key
- **Where:** `koan_settings.js:276-296`
- **What's wrong:** If `setShortcut` resolves `false`, nothing is shown, `editor.saved = true`, and on blur `showKeys` is not called. The field keeps displaying a key that isn't bound. This happens with keyCode 0 or 229 (IME/unidentified keys pass `isValidKey` but fail `FromPrefValue`), or if the browser-side check finds a conflict the stale client state missed.
- **Fix:** On `false`, call `showKeys(input, action)` and show the invalid note. After a conflict refusal, also restore the bound key after blur. Zen leaves the text too, but Koan's "Unsaved" path already restores, so be consistent.

### N3. Shortcut label for Ctrl+, differs from Zen's
- **Where:** `koan_shortcut_table.cc:81`
- **What's wrong:** The label is "Open Settings". Zen's string is `zen-preferences-shortcut = Open Preferences`, and USAGE.md itself calls it Zen's "Open Preferences".
- **Fix:** Use Zen's label, since the issue asks for Zen's labels.

### N4. Reset to Default has no confirmation
- **Where:** `koan_settings.js:358-362`
- **What's wrong:** Zen's `resetAllShortcuts` asks for confirmation (with a restart prompt). Koan wipes all custom keys on a single click.
- **Fix:** Add a `confirm()` or an inline confirm.

### N5. Page and stylesheet links are relative, so nested paths render a broken page
- **Where:** `koan_settings.html:6-7` and `koan_settings_ui.cc:29-39`
- **What's wrong:** `href="koan_settings.css"` / `src="koan_settings.js"` are relative, and `ServeResource` serves HTML for any path except the two exact file names.
- **Effect:** `chrome://koan-settings/shortcuts/` or `/a/b` requests `/a/koan_settings.js`, receives HTML (with a JS MIME type), and shows a blank page.
- **Fix:** Use `/koan_settings.css` and `/koan_settings.js`.

### N6. The editor lists shortcuts for features switched off in chrome://flags
- **Where:** `koan_settings_handler.cc:305-313`
- **What's wrong:** Settings rows have `is_available`, but shortcut actions don't. With compact mode or workspaces disabled, their shortcuts are listed and editable even though `Run()` ignores them.
- **Fix:** Hide them or mark them.

### N7. The suspend flag is shared with extension keybindings
- **Where:** `koan_settings_handler.cc:219-233`
- **What's wrong:** `ExtensionKeybindingRegistryViews::OnShortcutHandlingSuspended` (extension_keybinding_registry_views.cc:87-89) sets the same FocusManager flag. If chrome://extensions/shortcuts is recording in another tab (it suspends all windows), focusing and blurring a Koan field clears the flag under it.
- **Note:** This is an edge case; a comment is enough.

### N8. Multi-window tint toggle *(uncertain, behaviour mostly pre-existing)*
- **Where:** `koan_workspaces.cc:297-302`
- **What's wrong:** Every window observes `kWorkspaceTint` and runs `UpdateTint()`, but the theme is profile-wide. With window A in a workspace and window B in "All tabs", turning the tint on lets whichever observer runs last decide. If that is B, it restores the user colour, so A's tint doesn't appear until A's next focus change.
- **Fix:** Only the last-active window should apply (e.g. check `browser_view_->IsActive()`, or route the update through the most recently active browser).

### N9. A malformed pref entry silently clears the shortcut
- **Where:** `koan_shortcut_table.cc:149-162`
- **What's wrong:** An override with an invalid key (hand-edited Preferences, or a future change to `IsValidCustomAccelerator`) counts as "custom, not set", so the action silently loses its default.
- **Fix:** Skip invalid entries, so the default applies.

### N10. GN dep out of order
- **Where:** `src/browser/ui/views/BUILD.gn:86`
- **What's wrong:** `"//chrome/browser/ui:singleton_tabs"` was appended after the `//koan/...` deps instead of in sorted position. The file was already partly unsorted, but don't add to it.

### N11. Python line length
- **Where:** `embed_resources.py:39`
- **What's wrong:** The line is over 80 columns.
- **Fix:** Wrap it.

### N12. Missing include
- **Where:** `koan_shortcut_table.h:81`
- **What's wrong:** It uses `std::string_view` without `#include <string_view>` and relies on a transitive include from `base/values.h`.

### N13. Trivial wrapper
- **Where:** `koan_settings_handler.cc:70-72`
- **What's wrong:** `KeyValue()` only forwards to `shortcuts::ToPrefValue`.
- **Fix:** Inline it.

### N14. Unchecked argument access in handlers
- **Where:** `koan_settings_handler.cc:150, 172, 206, 212`
- **What's wrong:** `args[0]` is used unchecked, so a malformed `chrome.send` CHECK-crashes the browser. Only the WebUI's own renderer can send it, and this is the usual Chromium pattern, so the risk is low. A `CHECK_EQ(args.size(), N)` or early return makes the intent explicit.

### N15. Zen's Vertical Tabs group is missing and not listed as TODO
- **What's wrong:** Zen's "Look and Feel" has a **Vertical Tabs** group (show New Tab button on tab list, move it to the top, how to expand tabs; visible in `settings-zen-vs-koan.png`). It isn't declared as a group or listed in the TODO table. It is not in the issue checklist, so this is a parity nit only.

### N16. Fixed sleeps in the e2e test
- **Where:** `settings.mjs`
- **What's wrong:** The fixed `sleep(800-2500)` waits make the test slow and timing-dependent.
- **Fix:** Poll for the expected DOM or view state with a timeout.
