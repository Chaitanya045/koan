# Mac steps: dragging tabs into and out of folders, and folders by their rows (#372, #380)

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
| FD7 | Drag a normal tab over H's row (closed), middle; then away (#380) | H's icon opens (0.3 s) while the row is lit, closes again when the drop moves on. Same over G. |
| FD8 | Drag F by its row (open, not holding the active tab) | It closes as the drag starts (its tabs fold away); the image is F's row (closed icon, name). No search popup. |
| FD9 | Over H's top fifth, drop | The line on H's top; F lands above H, opens again; its tabs follow it. |
| FD10 | Drag F into H (H's middle, lit, icon opening), drop | F becomes H's subfolder, after H's tab; H opens (Zen expands the parents). |
| FD11 | Drag a folder over the normal tabs, over itself, over its own subfolder | No line, no lit row; a drop there puts it back. |
| FD12 | Make a chain five deep; drag a folder with a subfolder into the fourth level | No lit row (too deep); into the third: allowed. |
| FD13 | Drag a folder holding the active tab, drop | It stays closed after the drop, showing the active tab (dots on the icon). Esc mid-drag: back, open again unless it holds the active tab. |
| FD14 | Drag a folder over a Space button (or hold at the sidebar's edge), drop in the other Space's list | That Space shows; the folder moves there with its tabs. Side by side with Zen: FD8-FD10, light and dark. Known difference: Koan's folder keeps its tabs above its subfolders (a drop among a folder's tabs lands before its first subfolder, where the line shows). |
