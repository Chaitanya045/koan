// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_bookmark_spaces.h"

#include <algorithm>
#include <memory>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/strcat.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/bookmarks/managed_bookmark_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/browser/bookmark_utils.h"
#include "components/bookmarks/browser/titled_url_node.h"
#include "components/bookmarks/common/bookmark_pref_names.h"
#include "components/bookmarks/managed/managed_bookmark_service.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/prefs/pref_service.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/omnibox/koan_omnibox_hooks.h"
#include "koan/browser/ui/views/koan_bookmark_spaces_hooks.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_window_sync.h"
#include "koan/browser/ui/views/koan_window_sync_client.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/class_property.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/combobox_model.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/tree_node_iterator.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/view.h"

// A collapsed bookmarks bar button's focus behaviour before the collapse, + 1
// (0: not collapsed).
DEFINE_UI_CLASS_PROPERTY_KEY(int, kKoanCollapsedBookmarkFocusKey, 0)

namespace koan {

namespace {

using bookmarks::BookmarkModel;
using bookmarks::BookmarkNode;
using window_sync::SpaceRecord;
using window_sync::WindowSyncModel;

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kKoanBookmarkSpacesField);

// Zen's label for the editor's field (zen-bookmark-edit-panel-workspace-
// selector).
constexpr char16_t kSpacesLabel[] = u"Spaces";

// The profile's Spaces, where bookmarks can have some: the setting and window
// sync are on, the profile isn't a private one and has Spaces. Else null.
const WindowSyncModel* SpacesModel(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  PrefService* prefs = profile->GetPrefs();
  if (!prefs->GetBoolean(prefs::kBookmarksPerSpace) ||
      !prefs->GetBoolean(prefs::kWindowSyncEnabled)) {
    return nullptr;
  }
  const KoanWindowSync* sync = KoanWindowSync::ForProfile(profile);
  return sync && !sync->model().spaces().empty() ? &sync->model() : nullptr;
}

// How a Space is named in a list: its icon, then its name, as the sidebar.
std::u16string SpaceLabel(const SpaceRecord& space) {
  return space.icon.empty() ? space.name
                            : base::StrCat({space.icon, u" ", space.name});
}

// `ids` in the order of `spaces`, without the ids of Spaces that are gone.
std::vector<std::string> InSpaceOrder(const std::vector<SpaceRecord>& spaces,
                                      const std::set<std::string>& ids) {
  std::vector<std::string> ordered;
  for (const SpaceRecord& space : spaces) {
    if (ids.contains(space.id)) {
      ordered.push_back(space.id);
    }
  }
  return ordered;
}

// --- The bubble's row ---

// "All Spaces", then each Space; with a bookmark in several Spaces, those
// too, as the entry it starts on (pick one of the others to put it in just
// that one; "Spaces…" in its context menu picks several).
class SpacesComboboxModel : public ui::ComboboxModel {
 public:
  struct Entry {
    std::u16string label;
    std::vector<std::string> spaces;
  };

  SpacesComboboxModel(const std::vector<SpaceRecord>& spaces,
                      const std::vector<std::string>& current) {
    entries_.push_back({u"All Spaces", {}});
    for (const SpaceRecord& space : spaces) {
      entries_.push_back({SpaceLabel(space), {space.id}});
    }
    if (current.size() > 1) {
      std::u16string label;
      for (const SpaceRecord& space : spaces) {
        if (std::ranges::find(current, space.id) != current.end()) {
          label = label.empty()
                      ? SpaceLabel(space)
                      : base::StrCat({label, u", ", SpaceLabel(space)});
        }
      }
      entries_.push_back({label, current});
    }
    const auto it = std::ranges::find(entries_, current, &Entry::spaces);
    initial_ =
        it != entries_.end() ? static_cast<size_t>(it - entries_.begin()) : 0;
  }
  SpacesComboboxModel(const SpacesComboboxModel&) = delete;
  SpacesComboboxModel& operator=(const SpacesComboboxModel&) = delete;
  ~SpacesComboboxModel() override = default;

  size_t initial() const { return initial_; }
  const std::vector<std::string>& spaces_at(size_t index) const {
    return entries_[index].spaces;
  }

  // ui::ComboboxModel:
  size_t GetItemCount() const override { return entries_.size(); }
  std::u16string GetItemAt(size_t index) const override {
    return entries_[index].label;
  }
  std::optional<size_t> GetDefaultIndex() const override { return initial_; }

 private:
  std::vector<Entry> entries_;
  size_t initial_ = 0;
};

// --- The context menu's dialog ---

// A checkbox per Space.
class SpacesChecklist : public views::BoxLayoutView {
  METADATA_HEADER(SpacesChecklist, views::BoxLayoutView)

 public:
  SpacesChecklist(const std::vector<SpaceRecord>& spaces,
                  const std::vector<std::string>& checked) {
    SetOrientation(views::BoxLayout::Orientation::kVertical);
    SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStart);
    SetBetweenChildSpacing(views::LayoutProvider::Get()->GetDistanceMetric(
        views::DISTANCE_RELATED_CONTROL_VERTICAL));
    for (const SpaceRecord& space : spaces) {
      auto* box =
          AddChildView(std::make_unique<views::Checkbox>(SpaceLabel(space)));
      box->SetChecked(std::ranges::find(checked, space.id) != checked.end());
      boxes_.emplace_back(box, space.id);
    }
  }
  SpacesChecklist(const SpacesChecklist&) = delete;
  SpacesChecklist& operator=(const SpacesChecklist&) = delete;
  ~SpacesChecklist() override = default;

  // The ticked Spaces, in order.
  std::vector<std::string> Checked() const {
    std::vector<std::string> checked;
    for (const auto& [box, id] : boxes_) {
      if (box->GetChecked()) {
        checked.push_back(id);
      }
    }
    return checked;
  }

  base::WeakPtr<SpacesChecklist> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  std::vector<std::pair<raw_ptr<views::Checkbox>, std::string>> boxes_;
  base::WeakPtrFactory<SpacesChecklist> weak_factory_{this};
};

BEGIN_METADATA(SpacesChecklist)
END_METADATA

// The dialog's Save: what is ticked becomes the Spaces of every bookmark it
// was opened for (nothing changes if nothing was (un)ticked, so a selection
// whose bookmarks had different Spaces keeps them).
void SaveChecklist(base::WeakPtr<Profile> profile,
                   const std::vector<int64_t>& node_ids,
                   const std::vector<std::string>& before,
                   base::WeakPtr<SpacesChecklist> checklist) {
  if (!profile || !checklist) {
    return;
  }
  const std::vector<std::string> after = checklist->Checked();
  if (after == before) {
    return;
  }
  BookmarkModel* model =
      BookmarkModelFactory::GetForBrowserContext(profile->GetOriginalProfile());
  for (int64_t id : node_ids) {
    const BookmarkNode* node = bookmarks::GetBookmarkNodeByID(model, id);
    if (node && !node->is_permanent_node()) {
      KoanBookmarkSpaces::SetSpaces(model, node, after);
    }
  }
}

}  // namespace

// --- KoanBookmarkSpaces ---

KoanBookmarkSpaces::KoanBookmarkSpaces(BrowserView* browser_view)
    : browser_view_(browser_view) {}

KoanBookmarkSpaces::~KoanBookmarkSpaces() = default;

void KoanBookmarkSpaces::OnAddedToWidget() {
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view_);
  Profile* profile = browser_view_->GetProfile();
  if (!controller || !KoanWindowSync::ShouldSync(browser_view_)) {
    return;
  }
  if (BookmarkModel* model = BookmarkModelFactory::GetForBrowserContext(
          profile->GetOriginalProfile())) {
    model_observation_.Observe(model);
  }
  if (KoanWorkspaceController* spaces = controller->workspaces()) {
    active_space_subscription_ =
        spaces->AddSpacesChangedCallback(base::BindRepeating(
            &KoanBookmarkSpaces::Refresh, base::Unretained(this)));
  }
  if (KoanWindowSync* sync = KoanWindowSync::ForProfile(profile)) {
    synced_spaces_subscription_ =
        sync->AddSpacesChangedCallback(base::BindRepeating(
            &KoanBookmarkSpaces::Refresh, base::Unretained(this)));
  }
  pref_change_registrar_.Init(profile->GetPrefs());
  pref_change_registrar_.Add(prefs::kBookmarksPerSpace,
                             base::BindRepeating(&KoanBookmarkSpaces::Refresh,
                                                 base::Unretained(this)));
}

void KoanBookmarkSpaces::OnBrowserViewDestroying() {
  model_observation_.Reset();
  active_space_subscription_ = base::CallbackListSubscription();
  synced_spaces_subscription_ = base::CallbackListSubscription();
  pref_change_registrar_.RemoveAll();
}

// static
std::optional<bookmark_spaces::Filter> KoanBookmarkSpaces::FilterFor(
    BrowserWindowInterface* browser) {
  Profile* profile = browser ? browser->GetProfile() : nullptr;
  if (!profile || profile->IsOffTheRecord() ||
      !profile->GetPrefs()->GetBoolean(prefs::kBookmarksPerSpace)) {
    return std::nullopt;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  KoanWindowSyncClient* client =
      controller ? controller->window_sync() : nullptr;
  KoanWorkspaceController* spaces =
      controller ? controller->workspaces() : nullptr;
  if (!client || !client->is_synced() || !spaces || !spaces->IsEnabled()) {
    return std::nullopt;
  }
  const std::optional<tab_groups::TabGroupId> active = spaces->GetActiveSpace();
  if (!active) {
    return std::nullopt;
  }
  bookmark_spaces::Filter filter{.active = client->GetSpaceSyncId(*active)};
  if (filter.active.empty()) {
    return std::nullopt;
  }
  for (const SpaceRecord& space : client->sync()->model().spaces()) {
    filter.known.insert(space.id);
  }
  return filter;
}

// static
bool KoanBookmarkSpaces::IsShown(const BookmarkNode* node,
                                 const bookmark_spaces::Filter& filter) {
  for (; node && !node->is_permanent_node(); node = node->parent()) {
    std::string value;
    if (node->GetMetaInfo(bookmark_spaces::kMetaInfoKey, &value) &&
        !bookmark_spaces::IsShown(value, filter)) {
      return false;
    }
  }
  return true;
}

// static
std::vector<std::string> KoanBookmarkSpaces::GetSpaces(
    const BookmarkNode* node) {
  std::string value;
  return node && node->GetMetaInfo(bookmark_spaces::kMetaInfoKey, &value)
             ? bookmark_spaces::Parse(value)
             : std::vector<std::string>();
}

// static
void KoanBookmarkSpaces::SetSpaces(BookmarkModel* model,
                                   const BookmarkNode* node,
                                   const std::vector<std::string>& spaces) {
  if (!model || !node || node->is_permanent_node()) {
    return;
  }
  const std::string value = bookmark_spaces::Serialize(spaces);
  std::string old_value;
  const bool had = node->GetMetaInfo(bookmark_spaces::kMetaInfoKey, &old_value);
  if (value.empty()) {
    // Zen's bookmark of no Space: no key at all, as a bookmark from before.
    model->DeleteNodeMetaInfo(node, bookmark_spaces::kMetaInfoKey);
  } else if (!had || old_value != value) {
    model->SetNodeMetaInfo(node, bookmark_spaces::kMetaInfoKey, value);
  }
}

// static
void KoanBookmarkSpaces::OnSpaceDeleted(BrowserView* browser_view,
                                        const tab_groups::TabGroupId& space) {
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view);
  KoanWindowSyncClient* client =
      controller ? controller->window_sync() : nullptr;
  const std::string id = client && client->is_synced()
                             ? client->GetSpaceSyncId(space)
                             : std::string();
  BookmarkModel* model = BookmarkModelFactory::GetForBrowserContext(
      browser_view->GetProfile()->GetOriginalProfile());
  if (id.empty() || !model || !model->loaded()) {
    return;
  }
  // Collected first, then changed: observers run on each change.
  std::vector<const BookmarkNode*> nodes;
  ui::TreeNodeIterator<const BookmarkNode> it(model->root_node());
  while (it.has_next()) {
    const BookmarkNode* node = it.Next();
    const std::vector<std::string> spaces = GetSpaces(node);
    if (std::ranges::find(spaces, id) != spaces.end()) {
      nodes.push_back(node);
    }
  }
  for (const BookmarkNode* node : nodes) {
    std::string value;
    node->GetMetaInfo(bookmark_spaces::kMetaInfoKey, &value);
    SetSpaces(model, node,
              bookmark_spaces::Parse(bookmark_spaces::Without(value, id)));
  }
}

void KoanBookmarkSpaces::BookmarkMetaInfoChanged(const BookmarkNode* node) {
  Refresh();
}

void KoanBookmarkSpaces::BookmarkModelBeingDeleted() {
  model_observation_.Reset();
}

void KoanBookmarkSpaces::Refresh() {
  if (BookmarkBarView* bar = browser_view_->bookmark_bar()) {
    bar->InvalidateLayout();
    bar->SchedulePaint();
  }
}

// --- Hooks (koan_bookmark_spaces_hooks.h) ---

bool HidesBookmark(BrowserWindowInterface* browser, const BookmarkNode* node) {
  if (!node || node->is_permanent_node()) {
    return false;
  }
  const std::optional<bookmark_spaces::Filter> filter =
      KoanBookmarkSpaces::FilterFor(browser);
  return filter && !KoanBookmarkSpaces::IsShown(node, *filter);
}

bool CollapseBookmarkBarButton(BrowserWindowInterface* browser,
                               const BookmarkNode* node,
                               views::View* button,
                               int x,
                               int y,
                               bool fits) {
  const int saved_focus = button->GetProperty(kKoanCollapsedBookmarkFocusKey);
  if (!HidesBookmark(browser, node)) {
    if (saved_focus) {
      button->SetFocusBehavior(
          static_cast<views::View::FocusBehavior>(saved_focus - 1));
      button->GetViewAccessibility().SetIsIgnored(false);
      button->ClearProperty(kKoanCollapsedBookmarkFocusKey);
    }
    return false;
  }
  if (!saved_focus) {
    button->SetProperty(kKoanCollapsedBookmarkFocusKey,
                        static_cast<int>(button->GetFocusBehavior()) + 1);
    button->SetFocusBehavior(views::View::FocusBehavior::NEVER);
    button->GetViewAccessibility().SetIsIgnored(true);
  }
  // Visible while the buttons before it fit, as upstream's are: the first
  // hidden button stays the first one that didn't fit.
  button->SetVisible(fits);
  button->SetBounds(x, y, 0, 0);
  return true;
}

void AddBookmarkSpacesField(ui::DialogModel::Builder& builder,
                            Profile* profile,
                            const BookmarkNode* node) {
  const WindowSyncModel* model = SpacesModel(profile);
  if (!model || !node || node->is_permanent_node()) {
    return;
  }
  std::set<std::string> current;
  for (const std::string& id : KoanBookmarkSpaces::GetSpaces(node)) {
    current.insert(id);
  }
  builder.AddCombobox(
      kKoanBookmarkSpacesField, kSpacesLabel,
      std::make_unique<SpacesComboboxModel>(
          model->spaces(), InSpaceOrder(model->spaces(), current)),
      ui::DialogModelCombobox::Params().SetAccessibleName(
          u"The Spaces this bookmark shows in"));
}

void ApplyBookmarkSpacesField(ui::DialogModel& dialog,
                              BookmarkModel* model,
                              const BookmarkNode* node) {
  if (!dialog.HasField(kKoanBookmarkSpacesField)) {
    return;
  }
  ui::DialogModelCombobox* field =
      dialog.GetComboboxByUniqueId(kKoanBookmarkSpacesField);
  const auto* spaces =
      static_cast<SpacesComboboxModel*>(field->combobox_model());
  if (field->selected_index() != spaces->initial()) {
    KoanBookmarkSpaces::SetSpaces(model, node,
                                  spaces->spaces_at(field->selected_index()));
  }
}

void AddBookmarkSpacesItem(
    ui::SimpleMenuModel& menu,
    Profile* profile,
    const std::vector<raw_ptr<const BookmarkNode, VectorExperimental>>&
        selection) {
  if (selection.empty() || !SpacesModel(profile) ||
      !profile->GetPrefs()->GetBoolean(
          bookmarks::prefs::kEditBookmarksEnabled)) {
    return;
  }
  bookmarks::ManagedBookmarkService* managed =
      ManagedBookmarkServiceFactory::GetForProfile(profile);
  for (const BookmarkNode* node : selection) {
    if (node->is_permanent_node() ||
        (managed && managed->IsNodeManaged(node))) {
      return;
    }
  }
  menu.AddItem(kBookmarkSpacesCommand, u"Spaces…");
}

void ShowBookmarkSpacesDialog(
    gfx::NativeWindow parent,
    Profile* profile,
    const std::vector<raw_ptr<const BookmarkNode, VectorExperimental>>&
        selection) {
  const WindowSyncModel* model = SpacesModel(profile);
  if (!model || !parent || selection.empty()) {
    return;
  }
  // A Space is ticked if every selected bookmark is in it.
  std::vector<int64_t> node_ids;
  std::optional<std::set<std::string>> common;
  for (const BookmarkNode* node : selection) {
    node_ids.push_back(node->id());
    const std::vector<std::string> spaces = KoanBookmarkSpaces::GetSpaces(node);
    std::set<std::string> ids(spaces.begin(), spaces.end());
    if (!common) {
      common = std::move(ids);
    } else {
      std::erase_if(
          *common, [&ids](const std::string& id) { return !ids.contains(id); });
    }
  }
  const std::vector<std::string> before =
      InSpaceOrder(model->spaces(), common.value_or(std::set<std::string>()));
  auto checklist = std::make_unique<SpacesChecklist>(model->spaces(), before);
  base::WeakPtr<SpacesChecklist> checklist_ptr = checklist->GetWeakPtr();
  const bool one_folder = selection.size() == 1 && selection[0]->is_folder();
  std::unique_ptr<ui::DialogModel> dialog =
      ui::DialogModel::Builder(std::make_unique<ui::DialogModelDelegate>())
          .SetTitle(kSpacesLabel)
          .AddParagraph(ui::DialogModelLabel(
              selection.size() > 1 ? u"Show these bookmarks in:"
              : one_folder         ? u"Show this folder in:"
                                   : u"Show this bookmark in:"))
          .AddCustomField(
              std::make_unique<views::BubbleDialogModelHost::CustomView>(
                  std::move(checklist),
                  views::BubbleDialogModelHost::FieldType::kControl))
          .AddParagraph(ui::DialogModelLabel(
              u"With no Space ticked, it shows in every Space."))
          .AddOkButton(
              base::BindOnce(&SaveChecklist,
                             profile->GetOriginalProfile()->GetWeakPtr(),
                             std::move(node_ids), before, checklist_ptr),
              ui::DialogModel::Button::Params().SetLabel(u"Save"))
          .AddCancelButton(base::DoNothing())
          .Build();
  constrained_window::ShowBrowserModal(std::move(dialog), parent);
}

bool HidesBookmarkInMenuBar(Profile* profile, const BookmarkNode* node) {
  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
  return profile && browser && browser->GetProfile() == profile &&
         HidesBookmark(browser, node);
}

bool RebuildsBookmarkMenuOnOpen(Profile* profile) {
  return profile && !profile->IsOffTheRecord() &&
         profile->GetPrefs()->GetBoolean(prefs::kBookmarksPerSpace);
}

// koan_omnibox_hooks.h: the URL bar is in the active window.
bool HidesBookmarkSuggestion(const BookmarkModel* model,
                             const bookmarks::TitledUrlNode* node) {
  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
  if (!browser || !node ||
      BookmarkModelFactory::GetForBrowserContext(browser->GetProfile()) !=
          model) {
    return false;
  }
  // BookmarkModel::GetBookmarksMatching()'s nodes are its BookmarkNodes.
  return HidesBookmark(browser, static_cast<const BookmarkNode*>(node));
}

}  // namespace koan
