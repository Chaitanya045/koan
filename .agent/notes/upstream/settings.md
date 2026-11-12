# Upstream audit: chrome://settings and Koan's settings (2026-11-11)

What Chromium 158 offers, what Zen does, and what Koan locks, overrides,
redirects or reuses. Read before changing `src/browser/ui/webui/koan_settings_*`
or the settings hook patches.

## Zen 1.24b (behaviour only, MPL)

- Zen's panes are **in Firefox's own about:preferences document**:
  `preferences-xhtml.patch` adds nav buttons after General (Look and Feel,
  Tab Management, Keyboard Shortcuts, Mods) and `#include`s the pane markup;
  `preferences-js.patch` registers the panes with `register_module()`.
  Every groupbox carries `data-category="paneZen…"`, so Firefox's one search
  box (`findInPage` over all panes) finds Zen's settings and Firefox's
  together. There is one Settings page and one search.
- Zen hides Firefox UI it replaces or doesn't want (`zen-preferences.css`
  :403-432; inventory in `zen-firefox-overrides.md` A2 / Z28): data collection,
  website advertising, help/support, the browser layout subcategory, the theme
  group, AI controls, More from Mozilla, and Home while the URL bar replaces
  the new tab page.

## Chromium 158

- `SettingsUI` (`chrome/browser/ui/webui/settings/settings_ui.cc`) is one
  `MojoWebUIController` with ~50 `chrome.send` handlers on the `settings` data
  source (`webui::SetupWebUIDataSource`: CSP `script-src 'self'
  chrome://resources`, `style-src 'self'`, `img-src data:`, Trusted Types,
  `$i18n{}` replaced in JS and HTML). It sets **no request filter**
  (`WebUIDataSourceImpl::SetRequestFilter` CHECKs there is only one).
- The page (`chrome/browser/resources/settings`) is `settings-ui` > toolbar
  (`cr-toolbar` search) + `settings-menu` (left menu, static anchors) +
  `settings-main` (`cr-view-manager`, one view per section).
- **Extension point: `SettingsPlugin`** (`settings_main/settings_plugin.ts`):
  every top-level section is a plugin element in a `[slot=view]` of
  `settings-main`; a search calls each plugin's `searchContents(query)` (after
  `customElements.whenDefined()`), sums `matchCount`, toggles
  `hidden-by-search`, and shows "No results" only when all are empty.
- Routing (`route.ts`, `router.ts`): `Route.createSection(path, section)`;
  `Router.getInstance()` maps `location.pathname` to a route at start, falls
  back to `/` (BASIC) for unknown paths; BASIC shows `getTopLevelRoute()`
  (People). Search is `navigateTo(BASIC, ?search=q)`. `Router`, `routes` and
  `loadTimeData` are exported from `settings.js` (same module instance as the
  page's).
- Entry points: `chrome::ShowSettings*` / `ShowSettingsSubPage*`, `IDC_OPTIONS`
  (⋮ Settings, macOS Preferences…), page info Site settings, typed URLs,
  `chrome://help` → `/help` (`HandleWebUI`), aliases `/addresses`,
  `/searchEngines`, `/certificates` (`HandleWebUI`). All end on
  chrome://settings/<path>.
- URL rewriting (`BrowserURLHandler`) applies to **outermost main frames only**
  (`navigation_controller_impl.cc` `RewriteUrlForNavigation`); the first
  handler that returns true stops the chain.
- chrome://settings is refused in incognito (`incognito_allowed_url.cc`):
  it opens in the regular profile.
- **chrome://settings can't be framed by another WebUI page**: the frame
  could be allowed (`AddFrameAncestor`), but `WebUIImpl::SetUpMojoConnection`
  binds `chrome.send` for outermost main frames only (`web_ui_impl.cc:305`),
  so a framed settings app has no handlers.

## Decisions

| Upstream piece | Decision | How |
|---|---|---|
| chrome://settings, every section and sub-page | **reuse** as Koan's one Settings page | Koan's panes are added as a `SettingsPlugin` (`<koan-settings-page>`, one view `#koan`), sections `/koan/looks`, `/koan/tabs`, `/koan/shortcuts`, `/koan/agents` first in the menu |
| Toolbar search, `searchContents` | **reuse** | the plugin's `searchContents()` filters Koan's rows; Chromium's search covers its own sections. One box, one "No results" |
| `getTopLevelRoute()` (chrome://settings opens on People) | **override** | opens on Koan's Look and Feel, as Zen's ⌘, opens its first pane |
| chrome://koan-settings[/<pane>] (old Koan page) | **redirect** | a `BrowserURLHandler` rewrite to chrome://settings/koan/<pane> (regular profiles only); the separate WebUI is removed |
| ⌘, / Settings… (Koan's `kOpenSettings`) | **reuse** the singleton tab | `ShowSingletonTabOverwritingNTP(chrome://settings/koan/looks)` in the regular profile |
| ⋮ Settings, `IDC_OPTIONS`, `ShowSettingsSubPage`, page info, typed chrome://settings/<section> | **reuse** | they already open chrome://settings/<section>: now Koan's page, on the right section, with no redirect |
| Koan page's own nav, search box, "Chromium settings" and "Extensions and themes" links | **remove** (duplicate entry points) | Chromium's menu has both sections and an Extensions link |
| Message and listener names on the shared WebUI | **avoid silent collision** | `WebUIImpl::RegisterMessageCallback` keeps the first handler for a name, so Koan's are prefixed `koan…` / `koan-…` |
| Framing chrome://settings in Koan's page | **rejected** | no `chrome.send` in WebUI subframes (above) |
| Re-implementing Chromium's settings | **rejected** | hundreds of rows that change every milestone |
| Rows Zen hides (Z28: AI page, theme / home rows, sign-in) | **lock** (hidden) | #300: `showAiPage` and, while sign-in is off, `signinAvailable` false from `ExtendSettingsUI()` (no patch); Theme and Show home button through `pageVisibility` (`page_visibility.ts` patch; home only while the URL bar replaces the new tab page, Zen's rule). Ad privacy is gone upstream in 158; the metrics row is Google Chrome only. docs/SETTINGS_AUDIT.md |
| Chromium rows that Koan's layout makes do nothing (Appearance: Show bookmarks bar and its visibility menu, Show tab search button, Auto-pin new tab groups, hover card rows) | **lock** (hidden) | `hidden` in the `appearance_page.html.ts` patch, next to the tab strip position row; docs/SETTINGS_AUDIT.md |
| Chromium's Appearance > Mode (`browser.theme.color_scheme2`) vs the look's mode in the shadcn style | **reuse**, kept in step | `KoanColorSchemeSync` (no patch): one setting in two places |
| Chromium sections' look in the shadcn style | **override** | #301: tokens over the `--cr-*` variables in the document (`LookStylesheet()`), and `koan_chromium.css` (shadcn's Card, Switch, Select, Button, Input, sidebar menu) adopted into every shadow root by `koan_settings_page.js`; empty in the Zen style. At `/` the menu selects Look and Feel (`settings_menu.ts` patch) |

## Hooks (all in `patches/`, see docs/REBASE.md "Settings page")

- `chrome/browser/resources/settings/page_visibility.ts`: the Appearance rows
  Zen hides (Theme; Show home button while the URL bar replaces the new tab
  page).
- `chrome/browser/resources/settings/settings_menu/settings_menu.ts`: at `/`
  the top-level section's item is selected.
- `chrome/browser/ui/webui/settings/settings_ui.cc` (+ `settings/BUILD.gn`):
  `koan::settings::ExtendSettingsUI()` at the end of the constructor: Koan's
  handlers, the pane titles as strings, a request filter serving `koan/*`.
- `chrome/browser/resources/settings/settings_main/settings_main.ts`: imports
  `chrome://settings/koan_settings_page.js` (the plugin, served by the browser;
  the lint wants every element in a template imported), and `BUILD.gn`: the
  module's `.d.ts` in `ts_definitions` and the URL in `optimize_webui_excludes`.
- `chrome/browser/resources/settings/route.ts`: Koan's sections; top-level route.
- `chrome/browser/resources/settings/settings_menu/settings_menu.html`: Koan's
  menu items.
- `chrome/browser/resources/settings/settings_main/settings_main.html`: the
  plugin's view.
- `chrome/browser/chrome_content_browser_client.cc`: the koan-settings rewrite
  (`BrowserURLHandlerCreated`).

## Guards

- `scripts/tests/test_settings_shell.py`: the pane ids in `kPanes` match the
  routes, menu items and served file names in the patches; the JS calls only
  messages the handler registers.
- `koan_unittests` `SettingsUrlTest`: the rewrite.
- Mac steps in the PR (⌘,, ⋮ Settings, deep links, search, look).
