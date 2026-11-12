// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_import.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_tabrestore.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "components/sessions/core/serialized_user_agent_override.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "koan/browser/importer/koan_imported_spaces.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_folder_controller.h"
#include "koan/browser/ui/views/koan_folders.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_space_theme.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/gfx/range/range.h"

namespace koan {

namespace {

using importer::ImportedItem;
using importer::ImportedSpace;
using importer::ImportedSpaces;

// The window the import goes into, and its controllers.
struct Window {
  raw_ptr<BrowserWindowInterface> browser;
  raw_ptr<KoanWorkspaceController> spaces;
  raw_ptr<KoanPinnedTabController> pinned;
  // Null where folders are off: their tabs go loose in their Space.
  raw_ptr<KoanFolderController> folders;
};

// The profile's most recently active normal window with Spaces, as Zen's
// import takes the most recent browser window with workspaces.
std::optional<Window> FindWindow(Profile* profile) {
  std::optional<Window> found;
  ProfileBrowserCollection::GetForProfile(profile)->ForEach(
      [&](BrowserWindowInterface* browser) {
        BrowserView* browser_view =
            BrowserView::GetBrowserViewForBrowser(browser);
        KoanBrowserViewController* controller =
            browser_view ? KoanBrowserViewController::From(browser_view)
                         : nullptr;
        if (browser->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
            !controller || !controller->workspaces() ||
            !controller->workspaces()->IsEnabled() ||
            !controller->pinned_tabs()) {
          return true;
        }
        KoanFolderController* folders = controller->folders();
        found = Window{
            .browser = browser,
            .spaces = controller->workspaces(),
            .pinned = controller->pinned_tabs(),
            .folders = folders && folders->IsEnabled() ? folders : nullptr};
        return false;
      },
      BrowserCollection::Order::kActivation);
  return found;
}

// A theme as Koan stores it (koan_space_theme.h), from Zen's theme object.
std::string StoredTheme(const std::string& zen_theme) {
  const theme::SpaceTheme space_theme =
      theme::SpaceTheme::FromStoredJson(zen_theme);
  return space_theme.is_default()
             ? std::string()
             : base::WriteJson(space_theme.ToDict()).value_or(std::string());
}

// The tabs of `items`, folders' in order (the Essentials have no folders).
void Flatten(const std::vector<ImportedItem>& items,
             std::vector<ImportedItem>& tabs) {
  for (const ImportedItem& item : items) {
    if (item.is_folder) {
      Flatten(item.children, tabs);
    } else {
      tabs.push_back(item);
    }
  }
}

class SpaceImport {
 public:
  explicit SpaceImport(const Window& window) : window_(window) {}

  size_t Run(const ImportedSpaces& plan) {
    // The Essentials first, while Koan's 12 fit (Zen's import); the others
    // are pinned in the first Space.
    std::vector<ImportedItem> essentials;
    Flatten(plan.essentials, essentials);
    std::vector<ImportedItem> leftovers;
    for (const ImportedItem& tab : essentials) {
      if (window_->pinned->CanAddToEssentials()) {
        AddTab(tab, model()->IndexOfFirstNonPinnedTab(), std::nullopt, {});
      } else {
        leftovers.push_back(tab);
      }
    }
    size_t count = 0;
    for (const ImportedSpace& space : plan.spaces) {
      const std::optional<tab_groups::TabGroupId> id =
          MakeSpace(space, /*may_take_active=*/count == 0);
      if (!id) {
        break;
      }
      if (count == 0) {
        AddItems(leftovers, *id, 0, std::string(), 0);
        leftovers.clear();
      }
      AddItems(space.items, *id, 0, std::string(), 0);
      ++count;
    }
    if (const std::optional<tab_groups::TabGroupId> active =
            window_->spaces->GetActiveSpace();
        active && !leftovers.empty()) {
      AddItems(leftovers, *active, 0, std::string(), 0);
    }
    return count;
  }

 private:
  TabStripModel* model() const { return window_->browser->GetTabStripModel(); }

  // Whether `space` has pinned tabs or folders (Zen takes the active Space
  // for the first imported one only while it has none).
  bool HasPins(const tab_groups::TabGroupId& space) const {
    const TabGroup* group = model()->group_model()->GetTabGroup(space);
    const gfx::Range range = group->ListTabs();
    for (size_t index = range.start(); index < range.end(); ++index) {
      if (IsPinnedInSpace(model()->GetTabAtIndex(static_cast<int>(index)))) {
        return true;
      }
    }
    return window_->folders && !window_->folders->GetFolders(space).empty();
  }

  std::optional<tab_groups::TabGroupId> MakeSpace(const ImportedSpace& space,
                                                  bool may_take_active) {
    KoanWorkspaceController::SpaceData data{
        .name = space.name,
        .icon = space.icon,
        .theme = {.stored = StoredTheme(space.theme)}};
    KoanWorkspaceController* spaces = window_->spaces;
    if (const std::optional<tab_groups::TabGroupId> active =
            spaces->GetActiveSpace();
        may_take_active && active && !HasPins(*active)) {
      // It keeps its profile (container).
      if (const std::optional<KoanWorkspaceController::SpaceData> current =
              spaces->ReadSpace(*active)) {
        data.container = current->container;
      }
      spaces->WriteSpace(*active, data);
      spaces->ApplySpaceColor();
      return active;
    }
    // At the end of the Spaces, with an empty tab, as Zen's new Space.
    tabs::TabInterface* tab =
        EnsureEmptyTab(window_->browser, std::nullopt, model()->count(),
                       /*foreground=*/false);
    if (!tab) {
      return std::nullopt;
    }
    return spaces->CreateBackgroundSpace(model()->GetIndexOfTab(tab), data);
  }

  // Adds `items` to `space`, in folder `folder` (0: none, `token` empty).
  void AddItems(const std::vector<ImportedItem>& items,
                const tab_groups::TabGroupId& space,
                int folder,
                const std::string& token,
                int depth) {
    for (const ImportedItem& item : items) {
      if (!item.is_folder) {
        std::map<std::string, std::string> data;
        if (!token.empty()) {
          // It joins its folder as it is added (koan_folders.h).
          data[kFolderSessionKey] = token;
        }
        const TabGroup* group = model()->group_model()->GetTabGroup(space);
        AddTab(item,
               static_cast<int>(group->ListTabs().start()) + added_[space]++,
               space, std::move(data));
        continue;
      }
      // Without folders, or deeper than Koan's, a folder's tabs go where it
      // would be (as Zen's import flattens past its depth).
      if (!window_->folders || depth >= KoanFolderController::kMaxDepth) {
        AddItems(item.children, space, folder, token, depth);
        continue;
      }
      std::string child_token =
          base::Uuid::GenerateRandomV4().AsLowercaseString();
      const int child = window_->folders->AddSyncedFolder(
          child_token, item.title.empty() ? u"Folder" : item.title,
          item.collapsed, folder, space);
      AddItems(item.children, space, child, child_token, depth + 1);
    }
  }

  // An unloaded tab at `index`, in `space` (pinned to it) or else an
  // Essential, at its pinned URL with its title and label.
  void AddTab(const ImportedItem& item,
              int index,
              const std::optional<tab_groups::TabGroupId>& space,
              std::map<std::string, std::string> data) {
    KoanTabState state;
    state.pinned_url = item.url;
    state.pinned_in_space = space.has_value();
    state.custom_title = item.label;
    data[kPinnedTabSessionKey] = state.Serialize();
    sessions::SerializedNavigationEntry navigation;
    navigation.set_index(0);
    navigation.set_virtual_url(item.url);
    navigation.set_title(item.title);
    navigation.set_timestamp(base::Time::Now());
    chrome::AddRestoredTab(
        window_->browser, base::span_from_ref(navigation), index,
        /*selected_navigation=*/0, /*extension_app_id=*/std::string(), space,
        /*select=*/false, /*pin=*/!space, base::TimeTicks(), base::Time(),
        /*storage_namespace=*/nullptr, sessions::SerializedUserAgentOverride(),
        data, /*from_session_restore=*/true,
        /*is_active_browser=*/std::nullopt);
  }

  const raw_ref<const Window> window_;
  // The tabs added to each Space so far, after which the next one goes.
  std::map<tab_groups::TabGroupId, int> added_;
};

}  // namespace

size_t ImportSpaces(Profile* profile, const ImportedSpaces& plan) {
  if (plan.empty()) {
    return 0;
  }
  const std::optional<Window> window = FindWindow(profile);
  if (!window) {
    LOG(WARNING) << "Koan import: no window with Spaces to import them into";
    return 0;
  }
  return SpaceImport(*window).Run(plan);
}

}  // namespace koan
