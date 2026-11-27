// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_BOOKMARK_SPACES_HOOKS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_BOOKMARK_SPACES_HOOKS_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/base/models/dialog_model.h"
#include "ui/gfx/native_ui_types.h"

class BrowserWindowInterface;
class Profile;

namespace bookmarks {
class BookmarkModel;
class BookmarkNode;
}  // namespace bookmarks

namespace ui {
class SimpleMenuModel;
}

namespace views {
class View;
}

// Zen's bookmarks per Space (koan_bookmark_spaces.h): the hooks upstream's
// bookmark UI calls. Design and the map of every upstream surface that lists
// bookmarks: .agent/notes/upstream/bookmarks-per-space.md.
namespace koan {

// Whether `browser`'s window leaves out `node` (a bookmark or folder) where
// it lists bookmarks: it, or a folder it is in, belongs to other Spaces than
// the window's active one and not to that one. False where the setting is
// off, in a window outside window sync (private, blank) and for permanent
// folders. Called from BookmarkMenuDelegate::BuildNodeMenuItemAt() (the bar's
// folder menus, its overflow menu and All Bookmarks, the app menu's
// Bookmarks).
bool HidesBookmark(BrowserWindowInterface* browser,
                   const bookmarks::BookmarkNode* node);

// BookmarkBarView::Layout(), for the button of `node` at `x`: if the window
// leaves the bookmark out (HidesBookmark()), lays the button out with no
// room (visible as far as the bar's layout is concerned, so the bar's
// overflow logic, which takes the first hidden button for the first one
// that didn't fit, stays right; neither focusable nor in the accessibility
// tree) and returns true: the bar goes on with the next button at the same
// `x`. Otherwise gives the button back what a collapse took and returns
// false.
bool CollapseBookmarkBarButton(BrowserWindowInterface* browser,
                               const bookmarks::BookmarkNode* node,
                               views::View* button,
                               int x,
                               int y,
                               bool fits);

// The bookmark bubble (Cmd+D): Zen's Spaces row, under Folder. Nothing where
// the setting or window sync is off, or for a private profile.
void AddBookmarkSpacesField(ui::DialogModel::Builder& builder,
                            Profile* profile,
                            const bookmarks::BookmarkNode* node);
// Saves the row's choice to `node`, if the user changed it (called with the
// bubble's other edits, BookmarkBubbleDelegate::ApplyEdits()).
void ApplyBookmarkSpacesField(ui::DialogModel& dialog,
                              bookmarks::BookmarkModel* model,
                              const bookmarks::BookmarkNode* node);

// The bookmark context menu's "Spaces…" (BookmarkContextMenuController:
// the bar, its menus, the app menu): for bookmarks and folders, also several
// at once. Its command id; upstream's IDC_BOOKMARK_BAR_* are 51000-51028, so
// a clash with a new one fails to compile (a duplicate case label).
inline constexpr int kBookmarkSpacesCommand = 51030;
// Adds the item if `selection` can have Spaces (no permanent or managed
// folder, editing allowed) and the profile has Spaces to pick.
void AddBookmarkSpacesItem(
    ui::SimpleMenuModel& menu,
    Profile* profile,
    const std::vector<
        raw_ptr<const bookmarks::BookmarkNode, VectorExperimental>>& selection);
// Its dialog: a checkbox per Space.
void ShowBookmarkSpacesDialog(
    gfx::NativeWindow parent,
    Profile* profile,
    const std::vector<
        raw_ptr<const bookmarks::BookmarkNode, VectorExperimental>>& selection);

// The macOS menu bar's Bookmarks menu (BookmarkMenuBridge), which belongs to
// a profile, not a window: it follows the profile's most recently active
// window. Whether that leaves out `node` (HidesBookmark()).
bool HidesBookmarkInMenuBar(Profile* profile,
                            const bookmarks::BookmarkNode* node);
// Whether the menu is rebuilt each time it opens (what it shows depends on
// the active Space): while the setting is on.
bool RebuildsBookmarkMenuOnOpen(Profile* profile);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_BOOKMARK_SPACES_HOOKS_H_
