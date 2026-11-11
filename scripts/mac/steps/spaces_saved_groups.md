# Mac steps: a Space that was a saved tab group (#159, G8)

For the Mac loop and the hands-on tester. This is seeded test 11 of `scripts/mac/spaces/README.md`.

**The crash these steps guard against.** Koan disconnects a Space from an old profile's saved group (`KoanWorkspaceController::DisconnectSavedGroups()`), and the group's upstream `LocalTabGroupListener` is then dropped (`saved_tab_group_model_listener.cc.patch`). If the listener stayed, upstream would hit a **release** `CHECK(saved_group)` (`local_tab_group_listener.cc:163`) on the next tab move in that Space. Run these on a release build as well as a DCHECK build.

Note: closing a saved group's last *tab* deletes the saved group on desktop. To keep one as a closed saved group, close its *window* instead (this is how `seed11` closes Saved B).

**Setup.**
1. Run `python3 scripts/mac/spaces/seed_profiles.py <dir>`.
2. Do the printed seeding run for test 11.
3. Launch `test11-saved-groups` normally.

You get two Spaces from saved groups: "Saved A" is open at startup and reconnects as the window opens; "Saved B" is opened from the menu in step 1 of the table.

| # | Do | Expect |
|---|---|---|
| 1 | Open "Saved B": the app menu's Tab groups in the Chromium-toolbar layout (Ctrl+Alt+B), or the everything menu. Wait 1 s. | B opens as a Space and shows. |
| 2 | In Saved A, drag a tab up and down within the Space (reorder it) 5 times. Do the same in Saved B. | No crash. |
| 3 | Right-click a tab of another Space > Move To > Saved A. Then move it from Saved A to another Space. Repeat with Saved B. | The tab moves each time; no crash. |
| 4 | In Saved A, ⌘T and open a page; close a tab with ⌘W; close one from its close button. | No crash; the Space keeps its other tabs. |
| 5 | Rename Saved A in the header, give it an emoji, change its theme. | No crash; nothing changes in another window. |
| 6 | Drag a tab of Saved A to another window, and back. | No crash. |
| 7 | Close Saved B's last tab. | The Space stays, with a new tab (Koan's empty Space). Chrome's "Delete group?" never appears. |
| 8 | Delete Space Saved A (header menu > Delete Space > Yes). | Only Koan's "Delete Space?" asks; the Space goes; no crash. |
| 9 | Open Saved B from the menu again (known: it is a closed saved group again, so it opens as a second Space with the same name). Then close the window with ⌘W on its last tab, and quit with ⌘Q. | No crash at window close or quit. |
| 10 | DCHECK build: grep the log for `Spaces:` and `Check failed`. | Nothing. |
