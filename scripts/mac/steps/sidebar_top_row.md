# Sidebar top row and URL pill (Mac steps)

The scripted part is `scripts/mac/suite.sh --only sidebar_top_row` (real
clicks through AppKit). These are the steps it can't check.

1. Fresh profile, single toolbar. Open two pages in one tab. Click Back,
   Forward and Reload in the sidebar's top row with the mouse and with the
   trackpad: each navigates; the window doesn't move. A press-drag on the
   empty part of the row (between the toggle and Back) still moves the
   window, and a double-click there still zooms it.
2. Back and forward greyed when there is no history, reload never greyed.
3. Install uBlock Origin (or any extension with a badge) and pin it. Hover
   the sidebar: its button is in the pill with the badge's count, like Zen's.
   Click it: its popup opens at the button; the sidebar (compact mode: the
   peeked sidebar) stays while it is open.
4. Hover the sidebar: Copy URL (link) and Extensions (sliders) fade in after
   the extensions, in that order, as in Zen's pill. Copy URL: "Copied
   current URL!" toast, the address on the clipboard. Extensions: Chromium's
   extensions menu at the button; click the button again: it closes.
5. Compact mode with the sidebar peeked: steps 1, 3 and 4 again.
6. Tabs on the right: step 1 again (the row is mirrored).
