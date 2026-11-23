# Mac steps: `koan upload`, agents attach the files they were given (design section 9.4, #420)

For the Mac loop and the hands-on tester. DCHECK build, a fresh test profile, Agent ready on. The scripted part is
`scripts/mac/suite.sh --only agent_upload` (someone presses Allow when it pairs; real input on for T-up-9). Its files
are made in the run's own HOME. The rest is by hand, with `koan` from `Koan.app/Contents/Resources/bin/` paired as
"koan suite", on the fixture pages of `scripts/e2e/agent_upload.mjs` or a test site of your own. Use test files only.

| # | Do | Expect |
|---|---|---|
| T-up-1 | `koan open` a page with `<input type=file>`, `koan snapshot <tab> --interactive`, `koan act <tab> upload @<input> /tmp/resume.pdf`, with Koan's window in front and another app in front | "ok: attached resume.pdf (… KB)". No open panel or sheet appears, over any window; the frontmost app and the focused window don't change. The page shows the file (its name next to the input; the page's `change` handler runs). If the tab is shown, the pill reads "koan suite attached resume.pdf" for a few seconds; the window holding the tab shows the toast. |
| T-up-2 | `koan act <tab> click @<input>`, then `koan upload <tab> resume.pdf` (relative, from the file's folder) | The click's answer says the page waits with a file chooser and to answer it with `koan upload`; `koan status` lists `file-picker`; the pill offers Open / Dismiss. `koan upload` attaches it; the prompt goes (`koan watch` shows Koan.userNeededDone). |
| T-up-3 | An input with `multiple accept=".txt,image/png"`: upload two files; then a `.docx` | Both arrive, in order. The `.docx`: Koan.notAccepted naming the file and the list; nothing arrives. A single input given two paths: Koan.tooManyFiles. |
| T-up-4 | A hidden input behind a styled "Upload" button (LinkedIn's, a careers site's), and a drop zone that opens a chooser on click (Dropzone, a Greenhouse or Lever form) | `act upload @<button>` / `@<zone>` attaches the file; the site shows it as attached (its own file list or preview). In a longer batch (`fill … upload … click Submit`) the batch goes on past the upload. |
| T-up-5 | `upload` a path that doesn't exist, a folder, a file with mode 000, a file in `~/.ssh`, and one in the profile folder | Koan.fileNotFound, Koan.notAFile, Koan.fileUnreadable, Koan.notPermitted (twice), each naming the path; nothing is clicked. |
| T-up-6 | A file in `~/Downloads` (first time) | macOS asks once whether Koan may read Downloads; Allow, then it attaches. (The CLI's own check runs as the agent's terminal.) |
| T-up-7 | `koan upload <id of one of your own tabs> /tmp/resume.pdf` | Refused (not the agent's tab); your tab's page gets nothing. |
| T-up-8 | A folder input (`webkitdirectory`) | Koan.notSupported; the prompt stays for the user: Open shows the system folder panel as in Chrome. |
| T-up-9 | Take over the agent's tab (pill: Take over), then `koan upload <tab> /tmp/resume.pdf` | Koan.paused; nothing attached. Hand back: it works again. In a tab of yours taken over with `koan take-over` (Allow): uploads work; Take back: refused. |
| T-up-10 | An agent container tab (a task without "Your logins", Settings > Agent Control) | The upload works there too; the user's Open on its prompt shows the system panel. Other pickers (WebUSB, screen share) stay refused there. |
| T-up-11 | Settings > Agent Control > Activity | One `Koan.upload` line per upload with "resume.pdf (… KB)" and the tab's site; refusals as denied. No file contents, no folder paths. |
| T-up-12 | An agent on another machine: `scp resume.pdf <mac>:/tmp/`, then `ssh <mac> '~/Applications/Koan.app/Contents/Resources/bin/koan upload <tab> /tmp/resume.pdf'` | As T-up-1. |
| T-up-13 | `log stream --process Koan` during all of the above; ⌘Q with a chooser held | No CHECK/DCHECK (`FileSelectListener` answered twice), no crash. |
