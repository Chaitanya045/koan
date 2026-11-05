# Mac steps: the translucent window (glass) (#103)

For the Mac loop and the hands-on tester. DCHECK build, macOS 26 or later, "Translucent window" on (the default on Apple
Silicon). The blur and the colours can only be judged **on the real screen** (window captures render glass flat grey).
Put a busy window behind Koan (stripes, or text at 90 pt). After **every row**: `grep -E ':FATAL:|Check failed' <koan log>`
is empty, Koan is still running and there is no new `.ips`.

| # | Do | Expect |
|---|---|---|
| 1 | Open a window over the busy backdrop (dark mode) | The sidebar, the top row and the 8 pt gutter show the backdrop **blurred**, darkened by a black 40% tint. The page card is opaque. Light mode: bright blur, no tint. |
| 2 | Click another app (Finder), then back | While inactive: flat, no blur, no tint. Back: glass returns, no white flash, no black sidebar. ×10. |
| 3 | System Settings > Accessibility > Display > Reduce transparency on, then off, with Koan running | Opaque within about a second, glass back after. No crash. Repeat with compact mode on. |
| 4 | Enter and leave native fullscreen (Ctrl+⌘+F) ×10, with compact mode on and off | Opaque in fullscreen with **no one-frame desktop flash** on entry or exit; glass is back after. No CHECK, no black panel. |
| 5 | ⌘S (compact mode) on, hover the left edge and the top edge | The floating sidebar and toolbar have a **solid backing**, not see-through. Compact with the sidebar hidden but its header (traffic lights) kept: **no solid block behind the traffic lights**. |
| 6 | Settings > Look and Feel: "Translucent window" off, then on | Opaque at once, glass back at once, in every open window. |
| 7 | Open 3 windows, quit, relaunch with session restore | All 3 are glass **without clicking each one** (a window is glass from its creation). |
| 8 | Light mode (System Settings > Appearance), a window over the busy backdrop | The HUD material in light: judge against Zen side by side (only dark was measured). |
| 9 | A blank new tab | What shows through the card (Zen's card is transparent there; ours is opaque for now): note it. |
| 10 | Install a Web Store theme, then remove it | Note what glass does (Chromium turns it off with an extension theme). |
| 11 | Share the window in a Meet tab (window capture) | Glass off during the capture, back after, no crash. |
| 12 | DCHECK build: hover every sidebar row, open the URL bar, a toast, the Library and the welcome | No `Label` subpixel DCHECK, no `CustomCornersBackground` CHECK. |
| 13 | Energy (GT8): 3 glass windows, a video in the front one, 5 min, on battery with Battery Saver on | Compare Activity Monitor "Energy Impact" and WindowServer CPU with "Translucent window" off. Record both. If it is much worse, keep glass on the key window only. |
