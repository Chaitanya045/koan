---
name: koan-browser
description: Use Koan, the user's own web browser on their Mac, through the `koan` command. Open tabs of your own that use the user's logins, read and act on pages, take over a tab the user gives you. Use when a task needs a real, signed-in browser, or the user mentions Koan or their browser.
---

# Koan, the user's browser

Koan is the user's web browser. Agents use it with the `koan` command,
never with AppleScript or a remote debugging port.

Before any browser task, run `koan skill` and follow the guide it prints.
It comes from the installed Koan, so it is always current.

If `koan` is not found, use its full path:
`~/.local/bin/koan`, `~/Applications/Koan.app/Contents/Resources/bin/koan`
or `/Applications/Koan.app/Contents/Resources/bin/koan`. On another machine,
run it on the user's Mac over ssh, by that full path.
