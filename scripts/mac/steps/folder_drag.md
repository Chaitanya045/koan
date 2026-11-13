# Mac steps: dragging tabs into and out of folders (#372)

Hands-on, with Zen 1.24b beside it. Zen's reference: `ZenDragAndDrop.js` (`#applyDragoverIndicator()`,
`zen.tabs.folder-dragover-threshold-percent` 20, `#zen-dragover-background`), `ZenFolders.mjs`. Koan:
`PlaceInSpaceList()` / `FolderRowZone()` (`koan_space_drag_logic.h`), `KoanFolderController::DropInFolder()`.

Prepare: a Space with a pinned tab, folder F (open) holding two tabs and a collapsed subfolder G with one tab, a closed
folder H with one tab, and normal tabs.

| # | Do | Expect |
|---|---|---|
| FD1 | Drag a normal tab over F's row, top to bottom | Its top fifth: the line above F (drop: a pinned tab above the folders). The rest (F is open with three items): the row lit by a full-width bar 2 px short at the top and bottom, no line (drop: F's first tab, pinned). A tick as the target changes. |
| FD2 | Over H's row (closed) | Top fifth: line above H; bottom fifth: line below H (drop: after the folder, outside it); middle: H lit (drop: into H). |
| FD3 | Over F's first tab's bottom half, then G's row's bottom fifth | Line under that tab (drop: in F, after it); line under G (drop: in F, after G's subtree). |
| FD4 | Drag one of F's tabs onto the pinned tab's top half; then onto a normal tab | Out of F, pinned above; then out of F and unpinned. |
| FD5 | Drag an Essential into F (middle of its row) | It leaves the Essentials, keeps its pinned URL, F's first tab. |
| FD6 | Drag a tab into F, then press Esc before dropping | Nothing changes. |
| FD7 | Not yet in this PR (later folder step of #372): a collapsed folder's icon opening while hovered, dragging a folder by its row | Missing, as ZEN_PARITY says. |
