// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_stored_tab.h"

#include <utility>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"

namespace koan {

namespace {

constexpr char kUrlKey[] = "url";
constexpr char kTitleKey[] = "title";
constexpr char kDataKey[] = "data";
// The prefix of every Koan session data key (koan_session_data.cc).
constexpr char kKoanKeyPrefix[] = "koan.";

}  // namespace

StoredTab::StoredTab() = default;
StoredTab::StoredTab(GURL url,
                     std::u16string title,
                     std::map<std::string, std::string> data)
    : url(std::move(url)), title(std::move(title)), data(std::move(data)) {}
StoredTab::StoredTab(const StoredTab&) = default;
StoredTab& StoredTab::operator=(const StoredTab&) = default;
StoredTab::StoredTab(StoredTab&&) = default;
StoredTab& StoredTab::operator=(StoredTab&&) = default;
StoredTab::~StoredTab() = default;

bool StoredTab::operator==(const StoredTab&) const = default;

base::DictValue StoredTab::ToValue() const {
  base::DictValue values;
  for (const auto& [key, value] : data) {
    values.Set(key, value);
  }
  return base::DictValue()
      .Set(kUrlKey, url.spec())
      .Set(kTitleKey, title)
      .Set(kDataKey, std::move(values));
}

// static
std::optional<StoredTab> StoredTab::FromValue(const base::Value& value) {
  const base::DictValue* dict = value.GetIfDict();
  const std::string* url = dict ? dict->FindString(kUrlKey) : nullptr;
  if (!url) {
    return std::nullopt;
  }
  StoredTab tab;
  tab.url = GURL(*url);
  if (!tab.url.is_valid()) {
    return std::nullopt;
  }
  if (const std::string* title = dict->FindString(kTitleKey)) {
    tab.title = base::UTF8ToUTF16(*title);
  }
  if (const base::DictValue* values = dict->FindDict(kDataKey)) {
    for (const auto [key, data] : *values) {
      if (data.is_string() && base::StartsWith(key, kKoanKeyPrefix)) {
        tab.data.emplace(key, data.GetString());
      }
    }
  }
  return tab;
}

// static
std::vector<StoredTab> StoredTab::FromList(const base::ListValue* list) {
  std::vector<StoredTab> tabs;
  if (!list) {
    return tabs;
  }
  for (const base::Value& entry : *list) {
    if (std::optional<StoredTab> tab = FromValue(entry)) {
      tabs.push_back(std::move(*tab));
    }
  }
  return tabs;
}

}  // namespace koan
