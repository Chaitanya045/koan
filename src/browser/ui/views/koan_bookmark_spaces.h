// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_BOOKMARK_SPACES_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_BOOKMARK_SPACES_H_

#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "components/bookmarks/browser/base_bookmark_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/tab_groups/tab_group_id.h"
#include "koan/browser/ui/views/koan_bookmark_spaces_rule.h"

class BrowserView;
class BrowserWindowInterface;
class Profile;

namespace bookmarks {
class BookmarkModel;
class BookmarkNode;
}  // namespace bookmarks

namespace koan {

// Zen's bookmarks per Space (koan_bookmark_spaces_rule.h) in one window: a
// bookmark or folder can belong to some of the profile's Spaces (window
// sync's, the same in every window), and the bookmarks bar, the bookmark
// menus, the macOS Bookmarks menu and the URL bar's bookmark suggestions show
// those of the window's active Space and those that belong to none. The
// bookmark manager, the side panel and extensions see every bookmark. The
// hooks upstream calls are in koan_bookmark_spaces_hooks.h; this keeps the
// window's bookmarks bar in line (it lays out again when the active Space, a
// bookmark's Spaces, the profile's Spaces or the setting change). Owned by
// KoanBrowserViewController. Design:
// .agent/notes/upstream/bookmarks-per-space.md.
class KoanBookmarkSpaces : public bookmarks::BaseBookmarkModelObserver {
 public:
  explicit KoanBookmarkSpaces(BrowserView* browser_view);
  KoanBookmarkSpaces(const KoanBookmarkSpaces&) = delete;
  KoanBookmarkSpaces& operator=(const KoanBookmarkSpaces&) = delete;
  ~KoanBookmarkSpaces() override;

  // After the window's Spaces and its window sync client are set up.
  void OnAddedToWidget();
  void OnBrowserViewDestroying();

  // What `browser`'s window shows: its active Space's sync id and the
  // profile's Spaces. nullopt where every bookmark shows: the setting
  // (prefs::kBookmarksPerSpace) is off, the window isn't synced (private and
  // blank windows, window sync off, before it joins) or its active Space has
  // no sync id (an agent's Space).
  static std::optional<bookmark_spaces::Filter> FilterFor(
      BrowserWindowInterface* browser);
  // Whether `node` shows under `filter`: it and every folder it is in.
  static bool IsShown(const bookmarks::BookmarkNode* node,
                      const bookmark_spaces::Filter& filter);

  // The sync ids of the Spaces `node` belongs to (none: every Space).
  static std::vector<std::string> GetSpaces(
      const bookmarks::BookmarkNode* node);
  static void SetSpaces(bookmarks::BookmarkModel* model,
                        const bookmarks::BookmarkNode* node,
                        const std::vector<std::string>& spaces);

  // The user deleted `space` in `browser_view`'s window (before its tabs
  // close): it comes off every bookmark, and those left with no Space show
  // everywhere. Renaming a Space changes nothing (bookmarks name its id).
  static void OnSpaceDeleted(BrowserView* browser_view,
                             const tab_groups::TabGroupId& space);

 private:
  // bookmarks::BaseBookmarkModelObserver:
  void BookmarkModelChanged() override {}
  void BookmarkMetaInfoChanged(const bookmarks::BookmarkNode* node) override;
  void BookmarkModelBeingDeleted() override;

  // The bookmarks bar lays out again (its buttons collapse or come back).
  void Refresh();

  const raw_ptr<BrowserView> browser_view_;
  base::ScopedObservation<bookmarks::BookmarkModel,
                          bookmarks::BookmarkModelObserver>
      model_observation_{this};
  base::CallbackListSubscription active_space_subscription_;
  base::CallbackListSubscription synced_spaces_subscription_;
  PrefChangeRegistrar pref_change_registrar_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_BOOKMARK_SPACES_H_
