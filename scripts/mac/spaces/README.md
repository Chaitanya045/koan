# Spaces lockdown fixtures (PR #159)

Fixtures for the by-hand tests in `.agent/notes/upstream/tab-groups-omnibox-fullscreen.md` §1.6.

They use only throwaway profiles. No Web Store theme, no copy of anyone's Chrome profile, and no extension installed in a real profile: the test extension is loaded unpacked, into a seeded profile only.

```
python3 scripts/mac/spaces/seed_profiles.py ~/koan-spaces-fixtures
```

This writes one user data dir per test and prints the commands to run. `KOAN_APP` overrides the path of the Koan binary.

If `--load-extension` is ignored, load the extension instead from chrome://extensions: Developer mode, then Load unpacked, then pick `scripts/mac/spaces/test-extension`. Do this in the seeded profile's window only.

## Test 4: the G1 tint migration

`test4-tint-migration` is the profile an older Koan leaves when a red Space was active:
- `browser.theme.user_color2` is Koan's red tint;
- the user's own colour (green, tonal spot) is in `koan.workspaces.tint_saved_color` / `tint_saved_variant`;
- `koan.workspaces.tint_applied` is the red tint.

Expected after a launch:
- chrome://settings/appearance shows the green colour.
- The three `koan.workspaces.tint_*` prefs are gone from `Default/Preferences` (quit first, then `grep tint_ Default/Preferences` finds nothing).
- The default-theme Space shows green, not red.

## Test 6: Chrome-style groups (G3, G12)

`test6-chrome-groups` gets its session from a seeding run. Koan runs with its Spaces off (`--disable-features=KoanWorkspaces`), so upstream keeps the groups exactly as Chrome would, and the extension's `run('seed6')` makes:
- three groups: blue "Work" (2 tabs), red "Play" (1 tab, collapsed), and an unnamed grey one (1 tab);
- four ungrouped tabs.

Quit with ⌘Q, then launch without the flag. Expected:
- three Spaces: Work, Play and an unnamed one ("Space");
- all of them expanded: Play shows its tab when you switch to it;
- the four ungrouped tabs are in the Space that is shown;
- Work keeps blue and Play keeps red, because restored groups aren't foreign.

## Test 11: saved groups (G8)

`test11-saved-groups` is seeded the same way, with `run('seed11')`. With Spaces off, upstream saves every new group. "Saved A" stays open; "Saved B" is closed but still saved.

Quit, then launch without the flag. Expected:
- Turn the bookmarks bar on (Koan settings): it shows no saved-group chips.
- Saved A is a Space.
- Open Saved B from the app menu's Tab groups (Chromium toolbar layout: Ctrl+Alt+B) or the everything menu. It opens as a Space.
- In either Space, renaming, closing its last tab and Delete Space show only Koan's "Delete Space?", never Chrome's "Delete group?".

## Test 12: stored Spaces (G9)

`test12-stored-spaces` has startup set to "Open the New Tab page" and a stored Space list in `koan.workspaces.list`: Home, 🚀 Research (blue, the one shown) and Play (red). There is no session to restore.

Expected on launch:
- those three Spaces in that order, with their emoji and colours, each on a new tab;
- Research is shown.

## Test 15: the extension's tab group cases

Launch any profile with Spaces on (for example test 12's, after its test) with `--load-extension=scripts/mac/spaces/test-extension`. Then run `run('test15')` in the extension's service worker console, or click its toolbar button (the Chromium toolbar layout shows it).

Every line should be PASS:
- G12: `tabs.group()` makes a grey Space.
- G12: a colour the extension sets itself stays.
- G3: `tabGroups.update({collapsed: true})` is undone.
- `tabs.ungroup()`: the tab goes back into a Space.
- `tabGroups.move()`: the Space moves to the other window, and the source window keeps a Space.
- `tabs.highlight()` in another Space activates that tab.

The lines also go to `chrome.storage.local` under `result`.
