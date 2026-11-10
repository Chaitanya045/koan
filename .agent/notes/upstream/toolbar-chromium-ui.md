# Z15: Chromium-only UI in the multiple-toolbars layout

Date: 2026-11-11, pin 158. Row Z15 of `zen-firefox-overrides.md`. The
multiple-toolbars layout (Ctrl+Alt+B, `koan.single_toolbar` off) shows
Chromium's `ToolbarView` and its `LocationBarView`. The single toolbar hides
the toolbar, but the location bar it mounts in the sidebar's pill is the same
view. So each lock below applies to **both layouts**, as Zen's do.

What Zen 1.24b hides, in both layouts:
- `zen-omnibox.css:731-736`: `#reader-mode-button`, `#star-button-box`,
  `#pageActionButton`. Zen folds them into its site-data panel.
- `zen-omnibox.css:100`: `#identity-icon-label`.
- `zen-omnibox.css:505-508`: the protections icon
  (`zen.urlbar.show-protections-icon` false).
- `CustomizableUI-sys-mjs.patch`: no Firefox account button and no all-tabs
  button in the default placements.
- `zen-single-components.css:33-53`: the account CTAs in the app menu,
  Firefox View, the AI toggle and the private-window label.

## Upstream map (Chromium 158)

`chrome/browser/ui/views/toolbar/toolbar_view.cc`, `ToolbarView::Init()`:
- **Avatar** `AvatarToolbarButton` (`:602-607`). It is visible when
  `AvatarToolbarButtonInterface::CanShowForProfile()`, set only there. There
  is no pref or feature. `ToolbarController`'s overflow ignores a view whose
  owner hid it (`FlexLayout::CanBeVisible`).
- **Glic** (`:482-499`, `:564-600`): only if
  `glic::GlicEnabling::IsProfileEligible()`. With `features::kGlic` and
  `kGlicRollout` off (`koan_feature_overrides.cc`, O5) it is never built. The
  contextual tasks button (`:462-473`) is gated by
  `contextual_tasks::kContextualTasks`, also off.
- **Pinned toolbar actions** (`PinnedToolbarActionsContainer`,
  `toolbar/toolbar_pref_names.cc`): the default pins are Chrome Labs (no UI
  without flags), Tabs from other devices (feature off) and TTC (feature off).
  - Tab search (`kActionTabSearch`) and the side panel entries show here only
    if the user pins them (Customize Chrome, right-click).
  - An open side panel adds an ephemeral button for its entry.
- **App menu** (`AppMenuModel`, or `ActionAppMenu` behind
  `kAppMenuGlowUp`, off):
  - The sign-in and sync rows come from `ComputeSigninSectionInfo()`. It
    returns nothing unless `CanOfferSignin()`, which needs `kSigninAllowed`.
  - The profile row is a `ProfileSubMenuModel`.

Page actions (`LocationBarView`, `PageActionContainerView`, the
`page_action/` framework):
- **Star**, `kActionBookmarkThisTab`: shown by
  `BookmarkPageActionController::UpdatePageActionVisibility()` from
  `ShouldShowPageAction()`. Its `ActionItem` also backs the app menu's
  "Bookmark this tab" row (`action_app_menu_manager.cc:783`).
  `ToolbarView::ShowBookmarkBubble` `CHECK`s that the star's view exists, so
  the view must stay and only be hidden.
- **Reading mode**, `kActionSidePanelShowReadAnything`: shown by
  `ReadAnythingEntryPointController` per tab. The app menu uses another
  action, `kActionShowReadingModeSidePanel`.
- **Cookie "eye"**, `kActionShowCookieControls`: shown by
  `CookieControlsPageActionController` per tab.
- `PageActionModel::GetVisible()` requires the item's visibility, which
  `SetActionItemProperties` copies from the `ActionItem` on every change.
- **"Not secure" text**: `LocationIconView::GetText()` →
  `location_bar::GetSecurityChipText()`. It is the chip's label, its
  background (`UpdateBackground`) and its accessible name. With an empty
  label the accessible description is `GetSecureAccessibilityText()`.

## Decisions

| Item | Decision | How |
|---|---|---|
| Avatar (and the incognito chip) | **lock** | `KoanBrowserViewController::OnViewAddedToWidget` hides `toolbar()->avatar_toolbar_button()` after `ToolbarView::Init()`. No patch. The Profiles menu and the profile picker were already locked (#90, N-q). |
| ⋮ sign-in and sync rows | **override** | Default `prefs::kSigninAllowedOnNextStartup` = false (`OverrideServiceProfilePrefDefaults`), so `kSigninAllowed` is false from the next start. The app menu's sign-in and sync section, the avatar menu's and the bookmark bubble's promos all go. The user can re-allow it in Settings. Koan has no Google sync keys. |
| ⋮ profile row (`ProfileSubMenuModel`) | **not done** | No pref or feature removes it. A hook in `AppMenuModel::Build` would be more than a few lines, and the row still lists and opens profiles. Follow-up, with the rest of the app-menu audit (`zen-firefox-overrides.md` row "Toolbar contents in Mode::kNone"). |
| Glic | **reuse** | Already off (O5: `kGlic`, `kGlicRollout`, `kContextualTasks`). No button is built. |
| Tab search | **reuse (redirect)** | `kActionTabSearch` already runs the URL bar's tab search (`koan_command_policy.cc`, A9). It shows in the toolbar only if the user pins it, and then it is Koan's tab search. |
| Side panel | **reuse**; toolbar button **not done** | Koan keeps the side panel: ⌘B bookmarks (#90), reading list, history, reading mode. Zen's sidebar equivalent is its own panel. The ephemeral toolbar button while a panel is open would need its entries' items hidden. Those items also back menus, `side_panel_prefs` and the panel's header, or else a hook in `PinnedToolbarActionsContainer`. Follow-up, with T4 (Customize Chrome) in `themes-glass-ntp.md`. |
| Star | **lock (hook)** | `bookmark_page_action_controller.cc`: `ShouldShowPageAction()` returns false when `koan::HidesBookmarkStar()` (Koan's URL bar on). Not via its `ActionItem`, which the app menu's row and the bubble share. ⌘D, the Library and the menus still bookmark. |
| Reading mode icon | **lock** | Its `ActionItem` is hidden (`InstallCommandPolicy`). Reading mode still opens from Koan's reader-mode shortcut, the menus and the site panel. One side effect: Settings no longer lists reading mode among the side panels whose side can be set (`side_panel_prefs.cc:106`). |
| Cookie "eye" | **lock** | Its `ActionItem` is hidden (`InstallCommandPolicy`). The cookie controls stay in the site panel (page info), as Zen's protections live in its site-data panel. |
| "Not secure" / "Dangerous" / page-name text | **lock (hook)** | `location_icon_view.cc`: `GetText()` returns empty when `koan::HidesSecurityChipText()`. The icon (Koan's warning icon for http) and the page info bubble stay. Screen readers still hear the state. |

All of these apply where `features::IsUrlBarEnabled()` (Koan's URL bar), except
the avatar and the sign-in default, which apply always.

## Tests

- `src/test/browser/koan_toolbar_browsertest.cc`:
  - `NoAvatar`
  - `NoBrowserSignInByDefault`
  - `NoUrlBarExtras`: the star view is hidden on chrome://version and its
    item stays visible; the chip text is empty; the reading mode and cookie
    items are hidden.
- **Mac step (Z15):**
  1. Switch to multiple toolbars and open http://neverssl.com. Expected: only
     back, forward, reload, the URL bar, downloads and extensions. No text
     chip, no star, no avatar.
  2. ⌘D still opens the bookmark bubble.
  3. On a site with third-party cookies blocked, no eye icon. The site panel
     still has the cookie controls.
  4. On an article, no reading mode icon. the reader-mode shortcut and the site panel still open reading mode.
  5. ⋮ has no "Sign in to Koan" row.
