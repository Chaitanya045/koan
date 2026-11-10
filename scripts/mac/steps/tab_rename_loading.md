# Mac steps: rename any tab, the page-load bar, no tab spinner (#82)

For the Mac loop and the hands-on tester. A DCHECK build, a profile past the welcome, real pointer and keys. Zen's
reference: `ZenTabRenamer.mjs`, `ZenProgressBar.sys.mjs` + `#zen-loading-progress-bar`, `zen.theme.hide-tab-throbber`.

| # | Do | Expect |
|---|---|---|
| TR1 | Double-click a tab's label in the sidebar, type, Enter | Editor with the text selected; new label, toast "Tab has been successfully renamed!"; kept after a restart. |
| TR2 | Same, Esc; same with an empty label + Enter | Esc cancels; empty goes back to the page title. |
| TR3 | Double-click the favicon, the close button, an Essential | No rename. |
| TR4 | Double-click then keep moving the pointer | No tab drag; single clicks and drags still work. |
| TR5 | Right-click > Change Label…; then set `koan.tabs.rename_tabs` false in Preferences | Works; with the pref off neither the item nor the double-click. |
| LB1 | Load `httpbin.org/delay/5` | Pill pulsing centred in the gap above the page; at 3 s the wide track with the sweep; shrinks and fades at the end. Compare with Zen (light, dark, a Space theme). |
| LB2 | LB1 with the single toolbar off (Ctrl+Alt+B layout), in compact mode, in split view | 4 px inside the page top in the first; over the active pane in split view. |
| LB3 | A video in page fullscreen while loading; window fullscreen | No bar; bar shown. |
| LB4 | Click the page just under the bar | The page gets the click. |
| LB5 | Reduce Motion on, LB1 | No pulse/sweep, gone at once. |
| LB6 | Close the window mid-load; switch tabs mid-load | No crash; bar follows the selected tab. |
| TH1 | LB1, watch the tab row | Full-size favicon, no spinner, no shrink. `koan.theme.hide_tab_throbber` false + a new load: Chromium's spinner. |
