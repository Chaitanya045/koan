# Mac steps: agent control's layered indicator (#180, design section 6.2)

For the Mac loop and the hands-on tester. DCHECK build, a fresh profile, Agent ready on. The scripted part is
`scripts/mac/suite.sh --only agent_indicator` (someone presses Allow when it pairs, as for `agent_cli`); the rest is by
hand, with `koan` from `Koan.app/Contents/Resources/bin/` paired as "koan suite" (or Claude Code). Check both looks
(shadcn default, Zen) and light and dark.

| # | Do | Expect |
|---|---|---|
| 1 | Pair an agent and run `koan status` | A toast "koan suite connected" at the top corner (front window only). The sidebar shows a chip above the Space bar: a dot in the accent colour and "koan suite". No pill on the page. |
| 2 | `koan take-over` on the shown tab, Allow | On that tab: the pill at the page's bottom corner (away from the sidebar), "● koan suite is controlling this tab · Take back · Stop", and a thin accent border round the page (the content card's radius). The tab's sidebar row has a small dot at its top trailing corner. |
| 3 | Click links, select text and scroll in the bordered page | Every click lands in the page (the border takes no events); the first click pauses the tab: toast "koan suite paused: you used its tab", the pill says "paused: you used this tab" with Hand back, Take back, Stop. |
| 4 | Switch to another tab (click it in the sidebar, ⌃Tab) | No pill and no border there; the chip says "koan suite · 1 waiting" (the paused tab). Back on the taken-over tab: its pill again. |
| 5 | Click the chip | A menu: "koan suite (paused): <title>", then a separator, then Stop all agents. Pick the tab item: that tab is shown (its Space too, if another), with its pill. |
| 6 | Hand back, then `koan open https://example.com` and pick the new tab from the chip's menu | The agent's tab (in its Space) shows the pill with Take over and Stop (no Take back), and its row and the taken-over tab's row carry the dot. Take over: the pill says paused. |
| 7 | Compact mode hiding the sidebar (sidebar context menu > Compact Mode > Hide sidebar, then Enable compact mode), on a tab no agent holds | A small dot pill in the page's bottom corner (no text, or the waiting count). Click it: the same menu as the chip. Hover the sidebar out: the chip is there too. On the taken-over tab the pill shows instead of the dot. |
| 8 | Collapse the sidebar to icons; tabs on the right; native full screen (⌃⌘F) | Icons only: the chip shrinks to its dot. Tabs on the right: the pill and the dot go to the bottom-left corner. Full screen with the sidebar hidden: the dot pill shows. |
| 9 | A split (⌘⌥V) with the taken-over tab in one pane | The border is round that pane only; the pill shows while either pane is the active one. |
| 10 | Second window | It shows the chip (and the dot on its own agent tabs), never the other window's pill. |
| 11 | A page in the agent's tab calls `alert()` while another tab is shown | The chip says "· 1 waiting"; its menu's first item names the page ("a.test shows a message"); picking it shows the tab, whose pill has Open / Dismiss. |
| 12 | Stop (pill, chip menu or corner dot menu), then `koan end` on another pairing | Chip, pill, border, dots and corner dot all go; toast "… disconnected". |
| 13 | VoiceOver on the chip and the corner dot | Read as a button "koan suite is connected" (and "; 1 tab waits for you"); the pill is announced when it changes. |
| 14 | Close the window with the pill, the border and the menu open; ⌘Q | No crash, no CHECK/DCHECK in the log. |
| 15 | Auto-resume (design 9.2), defaults (on, 5 s): `koan open` a form page, show it, click into a text field and type a letter, then switch to another tab with ⌃Tab | The pill said "paused: continues when you stop using this tab · Keep paused · Hand back"; about 5 s after the letter `koan watch` shows `Koan.resumed {reason: "user-idle", changed: true}`, and a `koan wait <tab> --resumed` started before the switch printed "resumed: user-idle after 5 s; the page changed" and the form's snapshot. |
| 16 | Show the tab again, click the page once and keep the pointer still | After 5 s the pill reads "continues in 5s" and counts down each second, with Keep paused and Hand back; then the agent goes on. Move the pointer over the page during a countdown: the countdown ends and starts again 5 s later; it never pauses a driven tab by itself. |
| 17 | Click the page, then Keep paused | No countdown; it stays paused however long you wait or leave; `koan wait <tab> --resumed` exits 1 at once: "the user is handling this tab (keep-paused)". Hand back resumes it. |
| 18 | Click into a password field, and (on a checkout page) a card-number field; then leave the tab for 10 s | No resume; `koan tabs --json` shows `sticky: "secret-field"`. Likewise for Take over, an `alert()` waiting in the pill, a permission prompt and a security interstitial. |
| 19 | Settings > Agent Control: untick "When you stop using a tab, the agent continues", then 2 s and 60 s | Off: only Hand back resumes. 2 s and 60 s: the next pause waits that long (a pause already waiting picks the change up within a second). |
| 20 | VoiceOver on the countdown pill; close the window and ⌘Q mid-countdown | The pill is read once when the countdown starts, not every second; no crash, no CHECK/DCHECK in the log. |
