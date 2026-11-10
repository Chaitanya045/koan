// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_STORED_TAB_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_STORED_TAB_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/values.h"
#include "url/gurl.h"

namespace koan {

// A Space's pinned tab as kept in its koan.workspaces.list entry, so that a
// start that restores no session (On startup set to the New Tab page or to
// pages) brings it back, unloaded, as Zen does (ZenSessionManager's
// "restoring only pinned tabs"). `data` is the tab's Koan session data
// (koan_session_data.h): its pinned URL, label, icon and container.
struct StoredTab {
  StoredTab();
  StoredTab(GURL url,
            std::u16string title,
            std::map<std::string, std::string> data);
  StoredTab(const StoredTab&);
  StoredTab& operator=(const StoredTab&);
  StoredTab(StoredTab&&);
  StoredTab& operator=(StoredTab&&);
  ~StoredTab();

  bool operator==(const StoredTab&) const;

  base::DictValue ToValue() const;
  // nullopt for an entry that isn't a dict or whose URL isn't valid. Only
  // Koan's keys ("koan.") are kept from `data`.
  static std::optional<StoredTab> FromValue(const base::Value& value);
  // The valid entries of `list` (null: none), in order.
  static std::vector<StoredTab> FromList(const base::ListValue* list);

  GURL url;
  std::u16string title;
  std::map<std::string, std::string> data;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_STORED_TAB_H_
