// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/importer/koan_imported_spaces.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "base/containers/flat_map.h"
#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ref.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversion_utils.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "koan/browser/importer/koan_lz4.h"
#include "koan/browser/koan_space_title.h"
#include "url/url_constants.h"

namespace koan::importer {

ImportedItem::ImportedItem() = default;
ImportedItem::ImportedItem(const ImportedItem&) = default;
ImportedItem& ImportedItem::operator=(const ImportedItem&) = default;
ImportedItem::ImportedItem(ImportedItem&&) = default;
ImportedItem& ImportedItem::operator=(ImportedItem&&) = default;
ImportedItem::~ImportedItem() = default;

ImportedSpace::ImportedSpace() = default;
ImportedSpace::ImportedSpace(const ImportedSpace&) = default;
ImportedSpace& ImportedSpace::operator=(const ImportedSpace&) = default;
ImportedSpace::ImportedSpace(ImportedSpace&&) = default;
ImportedSpace& ImportedSpace::operator=(ImportedSpace&&) = default;
ImportedSpace::~ImportedSpace() = default;

ImportedSpaces::ImportedSpaces() = default;
ImportedSpaces::ImportedSpaces(const ImportedSpaces&) = default;
ImportedSpaces& ImportedSpaces::operator=(const ImportedSpaces&) = default;
ImportedSpaces::ImportedSpaces(ImportedSpaces&&) = default;
ImportedSpaces& ImportedSpaces::operator=(ImportedSpaces&&) = default;
ImportedSpaces::~ImportedSpaces() = default;

namespace {

// Trees deeper than this are cut (a cycle, a hostile file), as Zen's import
// (kMaxTreeDepth).
constexpr int kMaxDepth = 32;

// Arc's default Space (ZenMigrationUtils' kArcDefaultSpaceId).
constexpr char kArcDefaultSpaceId[] =
    "thebrowser.company.defaultPersonalSpaceID";

// A theme has at most this many colours (Zen's picker, kMaxSpaceColors).
constexpr size_t kMaxThemeColors = 3;

// More than any session file.
constexpr size_t kMaxSessionFileSize = 256u << 20;

// The pages a pinned tab may be (as Zen's import: kImportableSchemes).
bool IsImportableUrl(const GURL& url) {
  return url.is_valid() &&
         (url.SchemeIsHTTPOrHTTPS() || url.SchemeIs(url::kFtpScheme) ||
          url.SchemeIsFile());
}

// `icon` if it is an emoji, which Koan's Space icons are; else empty (Zen's
// SVG icons, chrome:// URLs; text).
std::u16string EmojiIcon(std::u16string icon) {
  const std::pair<std::u16string, std::u16string> split =
      SplitSpaceTitle(JoinSpaceTitle(icon, u"Space"));
  return !icon.empty() && split.first == icon && split.second == u"Space"
             ? icon
             : std::u16string();
}

std::u16string Trimmed(const std::string* text) {
  return text ? base::UTF8ToUTF16(
                    base::TrimWhitespaceASCII(*text, base::TRIM_ALL))
              : std::u16string();
}

// --- Arc ---------------------------------------------------------------------

// Arc stores Swift dictionaries as flat lists: key, value, key, value.
template <typename Fn>
void ForEachPair(const base::ListValue* list, Fn fn) {
  if (!list) {
    return;
  }
  for (size_t i = 0; i + 1 < list->size(); i += 2) {
    fn((*list)[i], (*list)[i + 1]);
  }
}

// The profile folder an Arc profile marker names: {"default": {}} for the
// main one, {"custom": {"_0": {"directoryBasename": ...}}} for the others.
std::string ArcProfileOf(const base::Value* marker) {
  if (const base::DictValue* dict = marker ? marker->GetIfDict() : nullptr) {
    if (const std::string* dir =
            dict->FindStringByDottedPath("custom._0.directoryBasename");
        dir && !dir->empty()) {
      return *dir;
    }
  }
  return "Default";
}

// The items of Arc's "global" container, by id.
class ArcItems {
 public:
  explicit ArcItems(const base::DictValue& container) {
    ForEachPair(container.FindList("items"),
                [this](const base::Value&, const base::Value& item) {
                  if (const base::DictValue* dict = item.GetIfDict()) {
                    if (const std::string* id = dict->FindString("id")) {
                      items_.emplace(*id, dict);
                      order_.push_back(dict);
                    }
                  }
                });
  }

  // The tabs and folders under item `id`, in order.
  std::vector<ImportedItem> Convert(const std::string& id, int depth) const {
    std::vector<ImportedItem> result;
    if (depth > kMaxDepth) {
      return result;
    }
    for (const base::DictValue* item : ChildrenOf(id)) {
      const base::DictValue* data = item->FindDict("data");
      const std::string* item_id = item->FindString("id");
      const std::string* title = item->FindString("title");
      if (!data || !item_id) {
        continue;
      }
      if (const base::DictValue* tab = data->FindDict("tab")) {
        const std::string* url = tab->FindString("savedURL");
        const std::string* saved_title = tab->FindString("savedTitle");
        ImportedItem entry;
        entry.url = url ? GURL(*url) : GURL();
        if (!IsImportableUrl(entry.url)) {
          continue;
        }
        entry.title = base::UTF8ToUTF16(saved_title && !saved_title->empty()
                                            ? *saved_title
                                        : title ? *title
                                                : std::string());
        result.push_back(std::move(entry));
      } else if (data->FindDict("list")) {
        // A folder; an empty one is left out. Zen's import shows Arc's
        // folders collapsed.
        ImportedItem folder;
        folder.is_folder = true;
        folder.title = title ? base::UTF8ToUTF16(*title) : std::u16string();
        folder.collapsed = true;
        folder.children = Convert(*item_id, depth + 1);
        if (!folder.children.empty()) {
          result.push_back(std::move(folder));
        }
      } else if (data->FindDict("splitView")) {
        // A split view's tabs, side by side where it is.
        std::ranges::move(Convert(*item_id, depth + 1),
                          std::back_inserter(result));
      }
    }
    return result;
  }

 private:
  // An item's children in its childrenIds order, else the items whose
  // parentID it is, in the file's order.
  std::vector<const base::DictValue*> ChildrenOf(const std::string& id) const {
    std::vector<const base::DictValue*> children;
    if (const auto it = items_.find(id); it != items_.end()) {
      if (const base::ListValue* ids = it->second->FindList("childrenIds")) {
        for (const base::Value& child_id : *ids) {
          if (const std::string* child = child_id.GetIfString()) {
            if (const auto child_it = items_.find(*child);
                child_it != items_.end()) {
              children.push_back(child_it->second);
            }
          }
        }
      }
    }
    if (children.empty()) {
      for (const base::DictValue* item : order_) {
        if (const std::string* parent = item->FindString("parentID");
            parent && *parent == id) {
          children.push_back(item);
        }
      }
    }
    return children;
  }

  // Into the parsed file, which outlives this.
  base::flat_map<std::string, const base::DictValue*> items_;
  std::vector<const base::DictValue*> order_;
};

// An Arc Space's emoji (customInfo.iconType: emoji_v2, else a code point).
std::u16string ArcSpaceIcon(const base::DictValue& space) {
  const base::DictValue* icon_type =
      space.FindDictByDottedPath("customInfo.iconType");
  if (!icon_type) {
    return std::u16string();
  }
  if (const std::string* emoji = icon_type->FindString("emoji_v2");
      emoji && !emoji->empty()) {
    return EmojiIcon(base::UTF8ToUTF16(*emoji));
  }
  std::u16string icon;
  if (const std::optional<int> code_point = icon_type->FindInt("emoji");
      code_point && base::IsValidCharacter(*code_point)) {
    base::WriteUnicodeCharacter(*code_point, &icon);
  }
  return EmojiIcon(std::move(icon));
}

using Rgb = std::array<int, 3>;

// An Arc colour ({red, green, blue, alpha}, 0 to 1) as 0-255 channels, or
// nullopt for a missing or fully transparent one.
std::optional<Rgb> ArcColor(const base::Value* value) {
  const base::DictValue* color = value ? value->GetIfDict() : nullptr;
  if (!color) {
    return std::nullopt;
  }
  const std::array<std::optional<double>, 3> channels = {
      color->FindDouble("red"), color->FindDouble("green"),
      color->FindDouble("blue")};
  if (!std::ranges::all_of(channels, [](const std::optional<double>& c) {
        return c && std::isfinite(*c);
      })) {
    return std::nullopt;
  }
  if (const std::optional<double> alpha = color->FindDouble("alpha");
      alpha && *alpha <= 0) {
    return std::nullopt;
  }
  Rgb rgb;
  for (size_t i = 0; i < rgb.size(); ++i) {
    rgb[i] =
        static_cast<int>(std::round(std::clamp(*channels[i], 0.0, 1.0) * 255));
  }
  return rgb;
}

// The dicts with a red, green and blue anywhere under `node`.
void CollectArcColors(const base::Value& node,
                      int depth,
                      std::vector<const base::Value*>& out) {
  if (depth > kMaxDepth) {
    return;
  }
  if (const base::DictValue* dict = node.GetIfDict()) {
    if (dict->Find("red") && dict->Find("green") && dict->Find("blue")) {
      out.push_back(&node);
      return;
    }
    for (const auto [key, child] : *dict) {
      CollectArcColors(child, depth + 1, out);
    }
  } else if (const base::ListValue* list = node.GetIfList()) {
    for (const base::Value& child : *list) {
      CollectArcColors(child, depth + 1, out);
    }
  }
}

// An Arc Space's window colours as Zen's theme object (ZenMigrationUtils'
// arcSpaceTheme() and spaceThemeFromArc()), JSON; empty without colours.
std::string ArcSpaceTheme(const base::DictValue& space) {
  const base::Value* style_value = space.FindByDottedPath(
      "customInfo.windowTheme.background.single._0.style.color._0");
  const base::DictValue* style =
      style_value ? style_value->GetIfDict() : nullptr;
  if (!style) {
    return std::string();
  }
  std::vector<const base::Value*> candidates;
  const base::DictValue* modifiers = nullptr;
  if (style->Find("blendedSingleColor")) {
    const base::DictValue* info =
        style->FindDictByDottedPath("blendedSingleColor._0");
    candidates.push_back(info ? info->Find("color") : nullptr);
    modifiers = info ? info->FindDict("modifiers") : nullptr;
  } else if (style->Find("blendedGradient")) {
    const base::DictValue* info =
        style->FindDictByDottedPath("blendedGradient._0");
    for (const char* key : {"baseColors", "overlayColors"}) {
      if (const base::ListValue* list = info ? info->FindList(key) : nullptr) {
        for (const base::Value& color : *list) {
          candidates.push_back(&color);
        }
      }
    }
    modifiers = info ? info->FindDict("modifiers") : nullptr;
  } else {
    CollectArcColors(*style_value, 0, candidates);
  }
  std::vector<Rgb> colors;
  for (const base::Value* candidate : candidates) {
    const std::optional<Rgb> color = ArcColor(candidate);
    if (color && !std::ranges::contains(colors, *color) &&
        colors.size() < kMaxThemeColors) {
      colors.push_back(*color);
    }
  }
  if (colors.empty()) {
    return std::string();
  }
  // Every dot at the primary colour's lightness, as Zen's import.
  const Rgb& primary = colors[0];
  const int lightness = static_cast<int>(
      std::round((std::ranges::max(primary) + std::ranges::min(primary)) / 2.0 /
                 255 * 100));
  base::ListValue gradient;
  for (size_t i = 0; i < colors.size(); ++i) {
    gradient.Append(base::DictValue()
                        .Set("c", base::ListValue()
                                      .Append(colors[i][0])
                                      .Append(colors[i][1])
                                      .Append(colors[i][2]))
                        .Set("isCustom", false)
                        .Set("algorithm", "")
                        .Set("isPrimary", i == 0)
                        .Set("lightness", lightness)
                        .Set("type", "explicit-lightness"));
  }
  const std::optional<double> intensity =
      modifiers ? modifiers->FindDouble("intensityFactor") : std::nullopt;
  const std::optional<double> noise =
      modifiers ? modifiers->FindDouble("noiseFactor") : std::nullopt;
  const double opacity = intensity && std::isfinite(*intensity)
                             ? std::clamp(*intensity, 0.3, 0.8)
                             : 0.5;
  const double texture =
      noise && std::isfinite(*noise)
          ? std::min(15.0 / 16,
                     std::round(std::clamp(*noise, 0.0, 1.0) * 16) / 16)
          : 0.0;
  return base::WriteJson(base::DictValue()
                             .Set("type", "gradient")
                             .Set("gradientColors", std::move(gradient))
                             .Set("opacity", opacity)
                             .Set("texture", texture))
      .value_or(std::string());
}

// --- Zen ---------------------------------------------------------------------

// Zen's theme object, as stored; empty for the default theme (no colours).
std::string ZenTheme(const base::DictValue* theme) {
  const base::ListValue* colors =
      theme ? theme->FindList("gradientColors") : nullptr;
  if (!colors || colors->empty()) {
    return std::string();
  }
  return base::WriteJson(*theme).value_or(std::string());
}

// A pinned tab of Zen's session: the URL it was pinned on
// (_zenPinnedInitialState), else the page it shows.
ImportedItem ZenTab(const base::DictValue& tab) {
  ImportedItem item;
  if (const base::DictValue* entry =
          tab.FindDictByDottedPath("_zenPinnedInitialState.entry")) {
    const std::string* url = entry->FindString("url");
    const std::string* title = entry->FindString("title");
    item.url = url ? GURL(*url) : GURL();
    item.title = title ? base::UTF8ToUTF16(*title) : std::u16string();
  }
  const base::ListValue* entries = tab.FindList("entries");
  if (!IsImportableUrl(item.url) && entries && !entries->empty()) {
    // Firefox's index is 1-based.
    const int index = std::clamp(tab.FindInt("index").value_or(1), 1,
                                 static_cast<int>(entries->size()));
    if (const base::DictValue* entry =
            (*entries)[static_cast<size_t>(index - 1)].GetIfDict()) {
      const std::string* url = entry->FindString("url");
      const std::string* title = entry->FindString("title");
      item.url = url ? GURL(*url) : GURL();
      item.title = title ? base::UTF8ToUTF16(*title) : std::u16string();
    }
  }
  if (const std::string* label = tab.FindString("zenStaticLabel")) {
    item.label = base::UTF8ToUTF16(*label);
  }
  return item;
}

// Zen's session file as an import plan. Folders are tab groups there: a tab
// names its folder (groupId), a folder its parent (parentId).
class ZenSessionReader {
 public:
  explicit ZenSessionReader(const base::DictValue& file) : file_(file) {}

  ImportedSpaces Read() {
    ReadSpaces();
    ReadFolders();
    ReadTabs();
    // Folders without tabs, in their Space.
    for (const std::string& id : folder_order_) {
      const Folder& folder = folders_.at(id);
      const std::optional<size_t> space = SpaceIndex(folder.space);
      if (!folder.skipped && !folder.split && space) {
        Place(id, *space, 0);
      }
    }
    for (size_t i = 0; i < plan_.spaces.size(); ++i) {
      plan_.spaces[i].items = Build(space_children_[i], 0);
    }
    return std::move(plan_);
  }

 private:
  struct Folder {
    std::string parent;
    std::string space;
    std::u16string name;
    bool collapsed = false;
    // A split view: its tabs go in the folder holding it.
    bool split = false;
    // Not a folder of pinned tabs: a live folder (its tabs come from a
    // feed) or one in the Essentials.
    bool skipped = false;
  };
  // A Space's or folder's child: a tab (of `tabs_`) or a folder.
  struct Entry {
    bool is_folder = false;
    size_t tab = 0;
    std::string folder;
  };

  void ReadSpaces() {
    const base::ListValue* spaces = file_->FindList("spaces");
    if (!spaces) {
      return;
    }
    for (const base::Value& value : *spaces) {
      const base::DictValue* space = value.GetIfDict();
      const std::string* uuid = space ? space->FindString("uuid") : nullptr;
      if (!uuid || uuid->empty() || std::ranges::contains(space_ids_, *uuid)) {
        continue;
      }
      ImportedSpace imported;
      imported.name = Trimmed(space->FindString("name"));
      if (imported.name.empty()) {
        // The name Zen gives a Space it makes.
        imported.name = u"Space";
      }
      // An emoji, or one of Zen's SVG icons (a chrome:// URL), which Koan's
      // emoji icons can't show.
      if (const std::string* icon = space->FindString("icon")) {
        imported.icon = EmojiIcon(base::UTF8ToUTF16(*icon));
      }
      imported.theme = ZenTheme(space->FindDict("theme"));
      space_ids_.push_back(*uuid);
      plan_.spaces.push_back(std::move(imported));
    }
    space_children_.resize(plan_.spaces.size());
  }

  void ReadFolders() {
    const base::ListValue* folders = file_->FindList("folders");
    if (!folders) {
      return;
    }
    for (const base::Value& value : *folders) {
      const base::DictValue* dict = value.GetIfDict();
      const std::string* id = dict ? dict->FindString("id") : nullptr;
      if (!id || id->empty() || folders_.contains(*id)) {
        continue;
      }
      Folder folder;
      if (const std::string* parent = dict->FindString("parentId")) {
        folder.parent = *parent;
      }
      if (const std::string* space = dict->FindString("workspaceId")) {
        folder.space = *space;
      }
      folder.name = Trimmed(dict->FindString("name"));
      folder.collapsed = dict->FindBool("collapsed").value_or(false);
      folder.split = dict->FindBool("splitViewGroup").value_or(false);
      folder.skipped = dict->FindBool("isLiveFolder").value_or(false) ||
                       dict->FindBool("essential").value_or(false);
      folders_.emplace(*id, std::move(folder));
      folder_order_.push_back(*id);
    }
  }

  void ReadTabs() {
    const base::ListValue* tabs = file_->FindList("tabs");
    if (!tabs) {
      return;
    }
    for (const base::Value& value : *tabs) {
      const base::DictValue* tab = value.GetIfDict();
      if (!tab || !tab->FindBool("pinned").value_or(false) ||
          tab->FindBool("zenIsEmpty").value_or(false) ||
          tab->FindBool("zenIsGlance").value_or(false)) {
        continue;
      }
      // A live folder's item.
      if (const std::string* live = tab->FindString("zenLiveFolderItemId");
          live && !live->empty()) {
        continue;
      }
      ImportedItem item = ZenTab(*tab);
      if (!IsImportableUrl(item.url)) {
        continue;
      }
      if (tab->FindBool("zenEssential").value_or(false)) {
        plan_.essentials.push_back(std::move(item));
        continue;
      }
      const std::string* workspace = tab->FindString("zenWorkspace");
      const std::optional<size_t> space =
          workspace ? SpaceIndex(*workspace) : std::nullopt;
      const std::string* group = tab->FindString("groupId");
      const std::optional<std::string> folder =
          group ? FolderOf(*group) : std::string();
      if (!space || !folder) {
        continue;
      }
      tabs_.push_back(std::move(item));
      const Entry entry{.tab = tabs_.size() - 1};
      if (folder->empty()) {
        space_children_[*space].push_back(entry);
      } else {
        Place(*folder, *space, 0);
        folder_children_[*folder].push_back(entry);
      }
    }
  }

  std::optional<size_t> SpaceIndex(const std::string& uuid) const {
    const auto it = std::ranges::find(space_ids_, uuid);
    if (it == space_ids_.end()) {
      return std::nullopt;
    }
    return static_cast<size_t>(std::distance(space_ids_.begin(), it));
  }

  // The folder a tab of group `id` goes in: `id`, or for a split view the
  // folder holding it; empty for none (a group that isn't a folder, a split
  // view outside folders); nullopt for a tab left out (a live folder's).
  std::optional<std::string> FolderOf(std::string id) const {
    for (int depth = 0; !id.empty() && depth <= kMaxDepth; ++depth) {
      const auto it = folders_.find(id);
      if (it == folders_.end()) {
        return std::string();
      }
      if (it->second.skipped) {
        return std::nullopt;
      }
      if (!it->second.split) {
        return id;
      }
      id = it->second.parent;
    }
    return std::string();
  }

  // Whether folder `id` is among its own parents (a broken file).
  bool IsOwnParent(const std::string& id) const {
    std::optional<std::string> up = FolderOf(folders_.at(id).parent);
    for (int depth = 0; up && !up->empty() && depth <= kMaxDepth; ++depth) {
      if (*up == id) {
        return true;
      }
      up = FolderOf(folders_.at(*up).parent);
    }
    return false;
  }

  // Puts folder `id` in its parent folder, else (or in a loop of parents) in
  // Space `space`, once; its parents first.
  void Place(const std::string& id, size_t space, int depth) {
    if (depth > kMaxDepth || !placed_.insert(id).second) {
      return;
    }
    const std::optional<std::string> parent =
        IsOwnParent(id) ? std::nullopt : FolderOf(folders_.at(id).parent);
    const Entry entry{.is_folder = true, .folder = id};
    if (parent && !parent->empty()) {
      Place(*parent, space, depth + 1);
      folder_children_[*parent].push_back(entry);
    } else {
      space_children_[space].push_back(entry);
    }
  }

  std::vector<ImportedItem> Build(const std::vector<Entry>& entries,
                                  int depth) {
    std::vector<ImportedItem> items;
    if (depth > kMaxDepth) {
      return items;
    }
    for (const Entry& entry : entries) {
      if (!entry.is_folder) {
        items.push_back(std::move(tabs_[entry.tab]));
        continue;
      }
      const Folder& folder = folders_.at(entry.folder);
      ImportedItem item;
      item.is_folder = true;
      item.title = folder.name;
      item.collapsed = folder.collapsed;
      if (const auto it = folder_children_.find(entry.folder);
          it != folder_children_.end()) {
        item.children = Build(it->second, depth + 1);
      }
      items.push_back(std::move(item));
    }
    return items;
  }

  const raw_ref<const base::DictValue> file_;
  ImportedSpaces plan_;
  std::vector<std::string> space_ids_;
  base::flat_map<std::string, Folder> folders_;
  std::vector<std::string> folder_order_;
  std::vector<ImportedItem> tabs_;
  std::vector<std::vector<Entry>> space_children_;
  std::map<std::string, std::vector<Entry>> folder_children_;
  std::set<std::string> placed_;
};

}  // namespace

ImportedSpaces ParseArcSpaces(std::string_view json,
                              std::string_view profile_dir_name) {
  ImportedSpaces plan;
  // Kept for the whole function: everything read points into it.
  const std::optional<base::DictValue> file =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const base::ListValue* containers =
      file ? file->FindListByDottedPath("sidebar.containers") : nullptr;
  // The window's sidebar is the "global" container (the others are Little
  // Arc windows' unpinned tabs).
  ForEachPair(containers, [&](const base::Value& kind,
                              const base::Value& value) {
    const base::DictValue* container = value.GetIfDict();
    if (!kind.is_dict() || !kind.GetDict().Find("global") || !container) {
      return;
    }
    const ArcItems items(*container);
    // The Favorites (top apps): Koan's Essentials.
    ForEachPair(
        container->FindList("topAppsContainerIDs"),
        [&](const base::Value& marker, const base::Value& id) {
          if (id.is_string() && ArcProfileOf(&marker) == profile_dir_name) {
            std::ranges::move(items.Convert(id.GetString(), 0),
                              std::back_inserter(plan.essentials));
          }
        });
    int index = 0;
    ForEachPair(container->FindList("spaces"), [&](const base::Value&,
                                                   const base::Value& value) {
      const base::DictValue* space = value.GetIfDict();
      if (!space) {
        return;
      }
      ++index;
      if (ArcProfileOf(space->Find("profile")) != profile_dir_name) {
        return;
      }
      ImportedSpace imported;
      const std::string* id = space->FindString("id");
      imported.name = Trimmed(space->FindString("title"));
      if (imported.name.empty()) {
        imported.name = id && *id == kArcDefaultSpaceId
                            ? u"Personal"
                            : u"Space " + base::NumberToString16(index);
      }
      imported.icon = ArcSpaceIcon(*space);
      imported.theme = ArcSpaceTheme(*space);
      std::optional<std::string> pinned;
      ForEachPair(space->FindList("containerIDs"),
                  [&](const base::Value& key, const base::Value& container_id) {
                    if (!pinned && key.is_string() &&
                        key.GetString() == "pinned" &&
                        container_id.is_string()) {
                      pinned = container_id.GetString();
                    }
                  });
      if (pinned) {
        imported.items = items.Convert(*pinned, 0);
      }
      plan.spaces.push_back(std::move(imported));
    });
  });
  return plan;
}

ImportedSpaces ParseZenSessions(std::string_view json) {
  // Kept for the whole function: the reader points into it.
  const std::optional<base::DictValue> file =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!file) {
    return ImportedSpaces();
  }
  return ZenSessionReader(*file).Read();
}

ImportedSpaces ReadZenSpaces(const base::FilePath& profile_dir) {
  std::string data;
  if (!base::ReadFileToStringWithMaxSize(
          profile_dir.AppendASCII(kZenSessionsFile), &data,
          kMaxSessionFileSize)) {
    return ImportedSpaces();
  }
  const std::optional<std::string> json = ReadMozLz4(base::as_byte_span(data));
  return json ? ParseZenSessions(*json) : ImportedSpaces();
}

}  // namespace koan::importer
