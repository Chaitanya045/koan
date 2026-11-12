// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/window_sync/koan_window_sync_model.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/strings/utf_string_conversions.h"

namespace koan::window_sync {

namespace {

template <typename Record>
Plan<Record> PlanRecords(const std::vector<Record>& local,
                         const std::vector<Record>& target) {
  Plan<Record> plan;
  std::map<std::string, const Record*> target_by_id;
  for (const Record& record : target) {
    target_by_id.emplace(record.id, &record);
  }
  std::map<std::string, const Record*> local_by_id;
  // What the window's order is once the removals are done.
  std::vector<std::string> kept;
  for (const Record& record : local) {
    // A duplicate id in one window (it can't come from the model): the
    // second one goes.
    if (!local_by_id.emplace(record.id, &record).second) {
      plan.remove.push_back(record.id);
      continue;
    }
    auto it = target_by_id.find(record.id);
    if (it == target_by_id.end()) {
      plan.remove.push_back(record.id);
      continue;
    }
    kept.push_back(record.id);
    if (!(*it->second == record)) {
      plan.update.push_back(*it->second);
    }
  }
  std::vector<std::string> target_order;
  for (size_t i = 0; i < target.size(); ++i) {
    target_order.push_back(target[i].id);
    if (!local_by_id.contains(target[i].id)) {
      plan.add.emplace_back(target[i], static_cast<int>(i));
    }
  }
  // Additions go in at their target index, so the order is only off if the
  // records the window kept are in another order.
  std::vector<std::string> kept_in_target_order;
  for (const std::string& id : target_order) {
    if (local_by_id.contains(id)) {
      kept_in_target_order.push_back(id);
    }
  }
  if (kept != kept_in_target_order) {
    plan.order = std::move(target_order);
  }
  return plan;
}

// The ids of `records`, for dropping the duplicates a window can't have.
template <typename Record>
void DropDuplicateIds(std::vector<Record>& records) {
  std::set<std::string> seen;
  std::erase_if(records, [&seen](const Record& record) {
    return record.id.empty() || !seen.insert(record.id).second;
  });
}

const char* KindName(ItemRecord::Kind kind) {
  return kind == ItemRecord::Kind::kEssential ? "essential" : "pinned";
}

}  // namespace

Plan<SpaceRecord> PlanSpaces(const std::vector<SpaceRecord>& local,
                             const std::vector<SpaceRecord>& target) {
  return PlanRecords(local, target);
}

namespace {

// Gives each Space of `local` the id of the first `free` Space of `target`
// with its name and icon, and takes that one.
std::vector<std::string> AdoptByName(const std::vector<SpaceRecord>& local,
                                     const std::vector<SpaceRecord>& target,
                                     std::vector<bool>& free) {
  std::vector<std::string> adopted(local.size());
  for (size_t l = 0; l < local.size(); ++l) {
    for (size_t t = 0; t < target.size(); ++t) {
      if (free[t] && target[t].name == local[l].name &&
          target[t].icon == local[l].icon) {
        adopted[l] = target[t].id;
        free[t] = false;
        break;
      }
    }
  }
  return adopted;
}

}  // namespace

std::vector<std::string> AdoptSpaces(const std::vector<SpaceRecord>& local,
                                     const std::vector<SpaceRecord>& target,
                                     const std::set<std::string>& taken) {
  std::vector<bool> free(target.size());
  for (size_t i = 0; i < target.size(); ++i) {
    free[i] = !taken.contains(target[i].id);
  }
  // Same name and icon first, so a window that recreated the Spaces from
  // prefs::kWorkspaceList keeps each one's tabs in it.
  std::vector<std::string> adopted = AdoptByName(local, target, free);
  size_t next = 0;
  for (size_t l = 0; l < local.size(); ++l) {
    if (!adopted[l].empty()) {
      continue;
    }
    while (next < target.size() && !free[next]) {
      ++next;
    }
    if (next == target.size()) {
      break;
    }
    adopted[l] = target[next].id;
    free[next] = false;
  }
  return adopted;
}

std::vector<std::string> AdoptSavedSpaces(const std::vector<SpaceRecord>& local,
                                          const std::vector<SpaceRecord>& saved,
                                          const std::set<std::string>& taken) {
  std::vector<bool> free(saved.size());
  for (size_t i = 0; i < saved.size(); ++i) {
    free[i] = !taken.contains(saved[i].id);
  }
  return AdoptByName(local, saved, free);
}

namespace {

// Two records of one folder that differ at most in `collapsed`, which Zen
// doesn't sync live (only through the session: new windows and restores).
bool SameButCollapsed(FolderRecord a, const FolderRecord& b) {
  a.collapsed = b.collapsed;
  return a == b;
}

}  // namespace

Plan<FolderRecord> PlanFolders(const std::vector<FolderRecord>& local,
                               const std::vector<FolderRecord>& target) {
  Plan<FolderRecord> plan = PlanRecords(local, target);
  // An existing folder keeps its own collapsed state; a new one takes the
  // target's.
  std::erase_if(plan.update, [&local](const FolderRecord& record) {
    return std::ranges::any_of(local, [&record](const FolderRecord& mine) {
      return mine.id == record.id && SameButCollapsed(mine, record);
    });
  });
  return plan;
}

Plan<ItemRecord> PlanItems(const std::vector<ItemRecord>& local,
                           const std::vector<ItemRecord>& target) {
  return PlanRecords(local, target);
}

WindowSyncModel::WindowSyncModel() = default;
WindowSyncModel::~WindowSyncModel() = default;

bool WindowSyncModel::SetSpaces(std::vector<SpaceRecord> spaces) {
  DropDuplicateIds(spaces);
  // Zen keeps at least one Space: a window reporting none is closing or not
  // set up yet, not a profile without Spaces.
  if (spaces.empty()) {
    return false;
  }
  const bool was_seeded = seeded_;
  seeded_ = true;
  if (was_seeded && spaces == spaces_) {
    return false;
  }
  spaces_ = std::move(spaces);
  DropOrphans();
  return true;
}

bool WindowSyncModel::SetFolders(std::vector<FolderRecord> folders) {
  DropDuplicateIds(folders);
  const std::vector<FolderRecord> folders_before = std::move(folders_);
  const std::vector<ItemRecord> items_before = items_;
  folders_ = std::move(folders);
  // (Tabs in a folder that went leave it.)
  DropOrphans();
  // Only collapsing or expanding a folder is kept (for new windows and
  // restores) but isn't a change the other windows apply.
  return !std::ranges::equal(folders_, folders_before, SameButCollapsed) ||
         items_ != items_before;
}

bool WindowSyncModel::SetItems(std::vector<ItemRecord> items,
                               const std::set<std::string>& closed) {
  DropDuplicateIds(items);
  // An item's Space must be in the model (the window reports its Spaces
  // first).
  std::set<std::string> space_ids;
  for (const SpaceRecord& space : spaces_) {
    space_ids.insert(space.id);
  }
  std::erase_if(items, [&space_ids](const ItemRecord& item) {
    return item.kind == ItemRecord::Kind::kPinned
               ? !space_ids.contains(item.space_id)
               : !item.space_id.empty();
  });
  // So must its folder (reported before it), in its Space.
  ClearUnknownFolders(items);
  if (items == items_) {
    return false;
  }
  std::set<std::string> ids;
  for (const ItemRecord& item : items) {
    ids.insert(item.id);
    closed_.erase(item.id);
  }
  for (const ItemRecord& item : items_) {
    if (!ids.contains(item.id) && closed.contains(item.id)) {
      closed_.insert(item.id);
    }
  }
  items_ = std::move(items);
  DropOrphans();
  return true;
}

void WindowSyncModel::ClearUnknownFolders(
    std::vector<ItemRecord>& items) const {
  std::map<std::string, std::string> folder_spaces;
  for (const FolderRecord& folder : folders_) {
    folder_spaces[folder.id] = folder.space_id;
  }
  for (ItemRecord& item : items) {
    auto it = folder_spaces.find(item.folder_id);
    if (item.kind != ItemRecord::Kind::kPinned || it == folder_spaces.end() ||
        it->second != item.space_id) {
      item.folder_id.clear();
    }
  }
}

WindowId WindowSyncModel::LiveWindow(const std::string& item) const {
  auto it = live_.find(item);
  return it == live_.end() ? kNoWindow : it->second;
}

void WindowSyncModel::SetLive(const std::string& item, WindowId window) {
  if (window == kNoWindow) {
    live_.erase(item);
    return;
  }
  if (std::ranges::any_of(items_, [&item](const ItemRecord& record) {
        return record.id == item;
      })) {
    live_[item] = window;
  }
}

ShowDecision WindowSyncModel::RequestShow(const std::string& item,
                                          WindowId window) const {
  if (window == kNoWindow ||
      std::ranges::none_of(items_, [&item](const ItemRecord& record) {
        return record.id == item;
      })) {
    return {};
  }
  const WindowId live = LiveWindow(item);
  if (live == window) {
    return {};
  }
  if (live == kNoWindow) {
    return {.kind = ShowDecision::Kind::kLoad};
  }
  return {.kind = ShowDecision::Kind::kMove, .from = live};
}

std::vector<std::string> WindowSyncModel::OnWindowClosing(WindowId window,
                                                          WindowId rescue_to) {
  std::vector<std::string> moved;
  for (auto& [item, live] : live_) {
    if (live == window && rescue_to != kNoWindow && rescue_to != window) {
      moved.push_back(item);
    }
  }
  return moved;
}

void WindowSyncModel::OnWindowClosed(WindowId window) {
  std::erase_if(live_,
                [window](const auto& entry) { return entry.second == window; });
}

base::DictValue WindowSyncModel::ToValue() const {
  base::ListValue spaces;
  for (const SpaceRecord& space : spaces_) {
    spaces.Append(base::DictValue()
                      .Set("id", space.id)
                      .Set("name", space.name)
                      .Set("icon", space.icon)
                      .Set("theme", space.theme)
                      .Set("container", space.container));
  }
  base::ListValue items;
  for (const ItemRecord& item : items_) {
    items.Append(base::DictValue()
                     .Set("id", item.id)
                     .Set("kind", KindName(item.kind))
                     .Set("space", item.space_id)
                     .Set("url", item.url)
                     .Set("title", item.custom_title)
                     .Set("icon", item.custom_icon)
                     .Set("container", item.container)
                     .Set("folder", item.folder_id));
  }
  base::ListValue folders;
  for (const FolderRecord& folder : folders_) {
    folders.Append(base::DictValue()
                       .Set("id", folder.id)
                       .Set("name", folder.name)
                       .Set("collapsed", folder.collapsed)
                       .Set("space", folder.space_id)
                       .Set("parent", folder.parent_id));
  }
  base::DictValue value;
  if (seeded_) {
    value.Set("spaces", std::move(spaces));
    value.Set("folders", std::move(folders));
    value.Set("items", std::move(items));
  }
  return value;
}

void WindowSyncModel::FromValue(const base::DictValue& value) {
  spaces_.clear();
  folders_.clear();
  items_.clear();
  live_.clear();
  closed_.clear();
  seeded_ = false;
  const base::ListValue* spaces = value.FindList("spaces");
  if (!spaces) {
    return;
  }
  const auto string_of = [](const base::DictValue& dict, const char* key) {
    const std::string* found = dict.FindString(key);
    return found ? *found : std::string();
  };
  std::vector<SpaceRecord> space_records;
  for (const base::Value& entry : *spaces) {
    const base::DictValue* dict = entry.GetIfDict();
    if (!dict) {
      continue;
    }
    space_records.push_back(
        {.id = string_of(*dict, "id"),
         .name = base::UTF8ToUTF16(string_of(*dict, "name")),
         .icon = base::UTF8ToUTF16(string_of(*dict, "icon")),
         .theme = string_of(*dict, "theme"),
         .container = string_of(*dict, "container")});
  }
  if (!SetSpaces(std::move(space_records))) {
    return;
  }
  std::vector<FolderRecord> folder_records;
  if (const base::ListValue* folders = value.FindList("folders")) {
    for (const base::Value& entry : *folders) {
      const base::DictValue* dict = entry.GetIfDict();
      if (!dict) {
        continue;
      }
      folder_records.push_back(
          {.id = string_of(*dict, "id"),
           .name = base::UTF8ToUTF16(string_of(*dict, "name")),
           .collapsed = dict->FindBool("collapsed").value_or(false),
           .space_id = string_of(*dict, "space"),
           .parent_id = string_of(*dict, "parent")});
    }
  }
  SetFolders(std::move(folder_records));
  std::vector<ItemRecord> item_records;
  if (const base::ListValue* items = value.FindList("items")) {
    for (const base::Value& entry : *items) {
      const base::DictValue* dict = entry.GetIfDict();
      if (!dict) {
        continue;
      }
      const std::string kind = string_of(*dict, "kind");
      if (kind != KindName(ItemRecord::Kind::kEssential) &&
          kind != KindName(ItemRecord::Kind::kPinned)) {
        continue;
      }
      item_records.push_back(
          {.id = string_of(*dict, "id"),
           .kind = kind == KindName(ItemRecord::Kind::kEssential)
                       ? ItemRecord::Kind::kEssential
                       : ItemRecord::Kind::kPinned,
           .space_id = string_of(*dict, "space"),
           .url = string_of(*dict, "url"),
           .custom_title = base::UTF8ToUTF16(string_of(*dict, "title")),
           .custom_icon = base::UTF8ToUTF16(string_of(*dict, "icon")),
           .container = string_of(*dict, "container"),
           .folder_id = string_of(*dict, "folder")});
    }
  }
  SetItems(std::move(item_records));
}

void WindowSyncModel::DropOrphans() {
  std::set<std::string> space_ids;
  for (const SpaceRecord& space : spaces_) {
    space_ids.insert(space.id);
  }
  // A folder's parent comes before it, in the same Space.
  std::map<std::string, std::string> kept_folders;
  std::erase_if(folders_, [&](const FolderRecord& folder) {
    const bool keep = space_ids.contains(folder.space_id) &&
                      (folder.parent_id.empty() ||
                       (kept_folders.contains(folder.parent_id) &&
                        kept_folders[folder.parent_id] == folder.space_id));
    if (keep) {
      kept_folders[folder.id] = folder.space_id;
    }
    return !keep;
  });
  std::erase_if(items_, [&space_ids](const ItemRecord& item) {
    return item.kind == ItemRecord::Kind::kPinned &&
           !space_ids.contains(item.space_id);
  });
  ClearUnknownFolders(items_);
  std::set<std::string> item_ids;
  for (const ItemRecord& item : items_) {
    item_ids.insert(item.id);
  }
  std::erase_if(live_, [&item_ids](const auto& entry) {
    return !item_ids.contains(entry.first);
  });
}

}  // namespace koan::window_sync
