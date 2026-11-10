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

std::vector<std::string> AdoptSpaces(const std::vector<SpaceRecord>& local,
                                     const std::vector<SpaceRecord>& target,
                                     const std::set<std::string>& taken) {
  std::vector<bool> free(target.size());
  for (size_t i = 0; i < target.size(); ++i) {
    free[i] = !taken.contains(target[i].id);
  }
  std::vector<std::string> adopted(local.size());
  // Same name and icon first, so a window that recreated the Spaces from
  // prefs::kWorkspaceList keeps each one's tabs in it.
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

bool WindowSyncModel::SetItems(std::vector<ItemRecord> items) {
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
  if (items == items_) {
    return false;
  }
  items_ = std::move(items);
  DropOrphans();
  return true;
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
                      .Set("theme", space.theme));
  }
  base::ListValue items;
  for (const ItemRecord& item : items_) {
    items.Append(base::DictValue()
                     .Set("id", item.id)
                     .Set("kind", KindName(item.kind))
                     .Set("space", item.space_id)
                     .Set("url", item.url)
                     .Set("title", item.custom_title)
                     .Set("icon", item.custom_icon));
  }
  base::DictValue value;
  if (seeded_) {
    value.Set("spaces", std::move(spaces));
    value.Set("items", std::move(items));
  }
  return value;
}

void WindowSyncModel::FromValue(const base::DictValue& value) {
  spaces_.clear();
  items_.clear();
  live_.clear();
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
         .theme = string_of(*dict, "theme")});
  }
  if (!SetSpaces(std::move(space_records))) {
    return;
  }
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
           .custom_icon = base::UTF8ToUTF16(string_of(*dict, "icon"))});
    }
  }
  SetItems(std::move(item_records));
}

void WindowSyncModel::DropOrphans() {
  std::set<std::string> space_ids;
  for (const SpaceRecord& space : spaces_) {
    space_ids.insert(space.id);
  }
  std::erase_if(items_, [&space_ids](const ItemRecord& item) {
    return item.kind == ItemRecord::Kind::kPinned &&
           !space_ids.contains(item.space_id);
  });
  std::set<std::string> item_ids;
  for (const ItemRecord& item : items_) {
    item_ids.insert(item.id);
  }
  std::erase_if(live_, [&item_ids](const auto& entry) {
    return !item_ids.contains(entry.first);
  });
}

}  // namespace koan::window_sync
