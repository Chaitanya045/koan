// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_space_routing.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_pinned_tabs.h"
#include "koan/browser/ui/views/koan_space_routes.h"
#include "koan/browser/ui/views/koan_spaces.h"
#include "koan/browser/ui/views/koan_toast.h"
#include "koan/browser/ui/views/koan_workspaces.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_model_host.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "url/url_constants.h"

namespace koan {

namespace {

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kKoanSpaceRoutingSave);

int g_external_links = 0;

// zen-space-routing.ftl and zen-library.ftl.
constexpr char16_t kTitle[] = u"Space Routing Settings";
constexpr char16_t kEmpty[] =
    u"Routes send links to the space you keep them in. Add one for a site you "
    u"always open in the same place.";
constexpr char16_t kMostRecent[] = u"Most recent space";
constexpr char16_t kExternal[] = u"Default route for external links";

constexpr int kRowSpacing = 8;
constexpr int kReferenceWidth = 220;

PrefService* PrefsOf(BrowserView* browser_view) {
  return browser_view->GetProfile()->GetOriginalProfile()->GetPrefs();
}

std::vector<SpaceRoute> ReadRoutes(PrefService* prefs) {
  return ParseSpaceRoutes(prefs->GetList(prefs::kSpaceRoutes));
}

// Zen's route editor ("When the link [contains] [...] Open it in [Space]"),
// all the routes at once, and the default route for external links. Save is
// off while a RegEx doesn't parse.
class KoanSpaceRoutingEditor : public views::BoxLayoutView,
                               public views::TextfieldController {
  METADATA_HEADER(KoanSpaceRoutingEditor, views::BoxLayoutView)

 public:
  KoanSpaceRoutingEditor(std::vector<std::u16string> spaces,
                         const std::vector<SpaceRoute>& routes,
                         std::u16string external_default)
      : spaces_(std::move(spaces)) {
    SetOrientation(views::BoxLayout::Orientation::kVertical);
    SetBetweenChildSpacing(kRowSpacing);
    empty_ = AddChildView(std::make_unique<views::Label>(kEmpty));
    empty_->SetMultiLine(true);
    empty_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    rows_ = AddChildView(std::make_unique<views::BoxLayoutView>());
    rows_->SetOrientation(views::BoxLayout::Orientation::kVertical);
    rows_->SetBetweenChildSpacing(kRowSpacing);
    for (const SpaceRoute& route : routes) {
      AddRow(route);
    }
    AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&KoanSpaceRoutingEditor::AddEmptyRow,
                            base::Unretained(this)),
        u"New route"));
    auto* external = AddChildView(std::make_unique<views::BoxLayoutView>());
    external->SetBetweenChildSpacing(kRowSpacing);
    external->SetCrossAxisAlignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    external->AddChildView(std::make_unique<views::Label>(kExternal));
    external_ = external->AddChildView(SpaceCombobox(external_default));
    external_->GetViewAccessibility().SetName(std::u16string(kExternal));
    UpdateEmpty();
  }

  void SetValidityCallback(base::RepeatingCallback<void(bool)> callback) {
    on_validity_ = std::move(callback);
    Validate();
  }

  std::vector<SpaceRoute> GetRoutes() const {
    std::vector<SpaceRoute> routes;
    for (const Row& row : rows_data_) {
      SpaceRoute route;
      route.reference = base::UTF16ToUTF8(row.reference->GetText());
      route.match = static_cast<SpaceRouteMatch>(
          row.match->GetSelectedIndex().value_or(0));
      route.space = SpaceAt(*row.space);
      routes.push_back(std::move(route));
    }
    return routes;
  }

  std::u16string GetExternalDefault() const { return SpaceAt(*external_); }

  base::WeakPtr<KoanSpaceRoutingEditor> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override {
    Validate();
  }

 private:
  struct Row {
    raw_ptr<views::View> view;
    raw_ptr<views::Combobox> match;
    raw_ptr<views::Textfield> reference;
    raw_ptr<views::Combobox> space;
  };

  // "Most recent space", then this window's Spaces, then `selected` if no
  // Space here has that name (a route for another window's Space keeps it).
  std::unique_ptr<views::Combobox> SpaceCombobox(
      const std::u16string& selected) {
    std::vector<ui::SimpleComboboxModel::Item> items;
    items.emplace_back(std::u16string(kMostRecent));
    size_t index = 0;
    for (const std::u16string& space : spaces_) {
      items.emplace_back(space);
      if (space == selected) {
        index = items.size() - 1;
      }
    }
    if (!selected.empty() && index == 0) {
      items.emplace_back(selected);
      index = items.size() - 1;
    }
    auto combobox = std::make_unique<views::Combobox>(
        std::make_unique<ui::SimpleComboboxModel>(std::move(items)));
    combobox->SetSelectedIndex(index);
    return combobox;
  }

  // The combobox's Space name, empty for the most recent Space.
  static std::u16string SpaceAt(const views::Combobox& combobox) {
    const size_t index = combobox.GetSelectedIndex().value_or(0);
    return index == 0 ? std::u16string()
                      : combobox.GetModel()->GetItemAt(index);
  }

  void AddEmptyRow() {
    AddRow(SpaceRoute());
    rows_data_.back().reference->RequestFocus();
  }

  void AddRow(const SpaceRoute& route) {
    auto* view = rows_->AddChildView(std::make_unique<views::BoxLayoutView>());
    view->SetBetweenChildSpacing(kRowSpacing);
    view->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    view->AddChildView(std::make_unique<views::Label>(u"When the link"));
    // In SpaceRouteMatch's order.
    auto* match = view->AddChildView(std::make_unique<views::Combobox>(
        std::make_unique<ui::SimpleComboboxModel>(
            std::vector<ui::SimpleComboboxModel::Item>{
                ui::SimpleComboboxModel::Item(u"Contains"),
                ui::SimpleComboboxModel::Item(u"Is Exactly"),
                ui::SimpleComboboxModel::Item(u"RegEx")})));
    match->SetSelectedIndex(static_cast<size_t>(route.match));
    match->SetCallback(base::BindRepeating(&KoanSpaceRoutingEditor::Validate,
                                           base::Unretained(this)));
    match->GetViewAccessibility().SetName(u"Match type");
    auto* reference = view->AddChildView(std::make_unique<views::Textfield>());
    reference->SetText(base::UTF8ToUTF16(route.reference));
    reference->SetPlaceholderText(u"example.com");
    reference->SetDefaultWidthInChars(0);
    reference->SetPreferredSize(
        gfx::Size(kReferenceWidth, reference->GetPreferredSize().height()));
    reference->SetController(this);
    reference->GetViewAccessibility().SetName(u"Link");
    view->AddChildView(std::make_unique<views::Label>(u"Open it in"));
    auto* space = view->AddChildView(SpaceCombobox(route.space));
    space->GetViewAccessibility().SetName(u"Open it in");
    view->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&KoanSpaceRoutingEditor::RemoveRow,
                            base::Unretained(this), base::Unretained(view)),
        u"Remove"));
    rows_data_.push_back({view, match, reference, space});
    UpdateEmpty();
    Validate();
  }

  void RemoveRow(views::View* view) {
    std::erase_if(rows_data_,
                  [view](const Row& row) { return row.view == view; });
    rows_->RemoveChildViewT(view);
    UpdateEmpty();
    Validate();
  }

  void UpdateEmpty() {
    empty_->SetVisible(rows_data_.empty());
    PreferredSizeChanged();
  }

  // Zen's regex is JavaScript's; Koan's is RE2's (no lookaround or
  // backreferences): a pattern RE2 can't use is marked, and Save waits.
  void Validate() {
    bool valid = true;
    for (const Row& row : rows_data_) {
      const bool invalid =
          row.match->GetSelectedIndex() ==
              static_cast<size_t>(SpaceRouteMatch::kRegex) &&
          !IsValidSpaceRouteRegex(base::UTF16ToUTF8(row.reference->GetText()));
      row.reference->SetInvalid(invalid);
      valid = valid && !invalid;
    }
    if (on_validity_) {
      on_validity_.Run(valid);
    }
  }

  const std::vector<std::u16string> spaces_;
  raw_ptr<views::Label> empty_ = nullptr;
  raw_ptr<views::BoxLayoutView> rows_ = nullptr;
  raw_ptr<views::Combobox> external_ = nullptr;
  std::vector<Row> rows_data_;
  base::RepeatingCallback<void(bool)> on_validity_;
  base::WeakPtrFactory<KoanSpaceRoutingEditor> weak_factory_{this};
};

BEGIN_METADATA(KoanSpaceRoutingEditor)
END_METADATA

void SaveRoutes(PrefService* prefs,
                base::WeakPtr<KoanSpaceRoutingEditor> editor) {
  if (!editor) {
    return;
  }
  prefs->SetList(prefs::kSpaceRoutes, SpaceRoutesToValue(editor->GetRoutes()));
  prefs->SetString(prefs::kSpaceRoutingExternalDefault,
                   base::UTF16ToUTF8(editor->GetExternalDefault()));
}

}  // namespace

// ScopedExternalLinks (koan_spaces.h).

ScopedExternalLinks::ScopedExternalLinks() {
  ++g_external_links;
}

ScopedExternalLinks::~ScopedExternalLinks() {
  --g_external_links;
}

// static
bool ScopedExternalLinks::IsActive() {
  return g_external_links > 0;
}

// Waits for an empty new tab's first navigation (Zen's initial-document
// case in ZenSpaceRoutingNavigation.sys.mjs).
class KoanSpaceRouting::FirstNavigationWatcher
    : public content::WebContentsObserver {
 public:
  FirstNavigationWatcher(KoanSpaceRouting* owner,
                         content::WebContents* contents)
      : content::WebContentsObserver(contents), owner_(owner) {}

  // content::WebContentsObserver:
  void DidStartNavigation(content::NavigationHandle* handle) override {
    if (!handle->IsInPrimaryMainFrame() || handle->IsSameDocument()) {
      return;
    }
    // (May delete this.)
    owner_->OnFirstNavigation(web_contents(), handle->GetURL());
  }

  void WebContentsDestroyed() override { owner_->StopWatching(web_contents()); }

 private:
  const raw_ptr<KoanSpaceRouting> owner_;
};

KoanSpaceRouting::KoanSpaceRouting(KoanWorkspaceController* controller,
                                   BrowserView* browser_view)
    : controller_(controller), browser_view_(browser_view) {}

KoanSpaceRouting::~KoanSpaceRouting() = default;

void KoanSpaceRouting::OnBrowserViewDestroying() {
  destroying_ = true;
  watchers_.clear();
}

void KoanSpaceRouting::OnTabInserted(tabs::TabInterface* tab,
                                     tabs::TabInterface* previous) {
  if (destroying_ || !tab || !controller_->IsEnabled() || tab->IsPinned() ||
      IsPinnedInSpace(tab)) {
    return;
  }
  content::WebContents* contents = tab->GetContents();
  const bool from_external = ScopedExternalLinks::IsActive();
  // Restored tabs stay in their Spaces; links from other apps go on being
  // routed while a startup restore runs (Zen's #shouldSkipProcessing()).
  if (!from_external &&
      SessionRestore::IsRestoring(browser_view_->GetProfile())) {
    return;
  }
  // A new page only: not a restored, duplicated or moved tab (they have
  // their history), nor one placed in another Space on purpose.
  content::NavigationController& navigation = contents->GetController();
  if (navigation.NeedsReload() || !navigation.IsInitialNavigation()) {
    return;
  }
  const std::optional<tab_groups::TabGroupId> space = tab->GetGroup();
  if (space && space != controller_->GetActiveSpace()) {
    return;
  }
  content::NavigationEntry* pending = navigation.GetPendingEntry();
  const GURL url = pending ? pending->GetURL() : contents->GetVisibleURL();
  if (url.SchemeIsHTTPOrHTTPS()) {
    // Not from inside the tab strip's notification.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&KoanSpaceRouting::RouteTab, weak_factory_.GetWeakPtr(),
                       tab->GetHandle(), url, from_external,
                       previous ? previous->GetHandle() : tabs::TabHandle()));
    return;
  }
  // window.open() and target=_blank tabs start empty: their first
  // navigation decides.
  if ((url.is_empty() || url.IsAboutBlank()) && !watchers_.contains(contents)) {
    watchers_.emplace(contents,
                      std::make_unique<FirstNavigationWatcher>(this, contents));
  }
}

void KoanSpaceRouting::OnFirstNavigation(content::WebContents* contents,
                                         const GURL& url) {
  if (url.IsAboutBlank()) {
    return;
  }
  if (tabs::TabInterface* tab =
          tabs::TabInterface::MaybeGetFromContents(contents);
      tab && url.SchemeIsHTTPOrHTTPS()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&KoanSpaceRouting::RouteTab, weak_factory_.GetWeakPtr(),
                       tab->GetHandle(), url, /*from_external=*/false,
                       tabs::TabHandle()));
  }
  StopWatching(contents);
}

void KoanSpaceRouting::StopWatching(content::WebContents* contents) {
  auto it = watchers_.find(contents);
  if (it == watchers_.end()) {
    return;
  }
  // Not while the watcher is running: it goes after its notification.
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, std::move(it->second));
  watchers_.erase(it);
}

std::optional<tab_groups::TabGroupId> KoanSpaceRouting::SpaceNamed(
    const std::u16string& name) const {
  for (const KoanWorkspaceController::SpaceInfo& info :
       controller_->GetSpaces()) {
    if (info.name == name) {
      return info.id;
    }
  }
  return std::nullopt;
}

std::optional<tab_groups::TabGroupId> KoanSpaceRouting::RouteFor(
    const GURL& url,
    bool from_external) const {
  if (destroying_ || !url.SchemeIsHTTPOrHTTPS()) {
    return std::nullopt;
  }
  PrefService* prefs = PrefsOf(browser_view_);
  const std::optional<std::u16string> name = RouteUrlToSpace(
      url.spec(), ReadRoutes(prefs), from_external,
      base::UTF8ToUTF16(prefs->GetString(prefs::kSpaceRoutingExternalDefault)));
  return name ? SpaceNamed(*name) : std::nullopt;
}

void KoanSpaceRouting::RouteTab(tabs::TabHandle handle,
                                const GURL& url,
                                bool from_external,
                                tabs::TabHandle previous) {
  tabs::TabInterface* tab = handle.Get();
  TabStripModel* model = controller_->tab_strip_model();
  if (destroying_ || !tab || tab->IsPinned() || IsPinnedInSpace(tab)) {
    return;
  }
  const int index = model->GetIndexOfTab(tab);
  if (index == TabStripModel::kNoTab) {
    return;
  }
  const std::optional<tab_groups::TabGroupId> target =
      RouteFor(url, from_external);
  if (!target || tab->GetGroup() == target) {
    return;
  }
  const std::u16string name = controller_->GetSpace(*target)->name;
  // Zen opens a routed tab in the background (shouldDeferTabSelection()):
  // the tab it came from stays in front, in its Space.
  bool stays_in_front = false;
  if (model->GetActiveTab() == tab) {
    tabs::TabInterface* back = previous.Get();
    if (!back || back == tab ||
        model->GetIndexOfTab(back) == TabStripModel::kNoTab) {
      back = model->GetOpenerOfTabAt(index);
    }
    if (back && back != tab &&
        (back->IsPinned() || back->GetGroup() == tab->GetGroup())) {
      model->ActivateTabAt(model->GetIndexOfTab(back));
    } else {
      // Nothing to go back to: the window follows the tab.
      stays_in_front = true;
    }
  }
  model->AddToExistingGroup({model->GetIndexOfTab(tab)}, *target,
                            /*add_to_end=*/true);
  if (stays_in_front) {
    controller_->SwitchTo(*target);
    return;
  }
  // Switching to the Space later lands on the routed tab
  // (lastSelectedWorkspaceTabs).
  controller_->SetLastActiveTab(*target, tab);
  if (KoanBrowserViewController* window =
          KoanBrowserViewController::From(browser_view_)) {
    window->toast()->Show(base::StrCat({u"New tab opened in ", name}));
  }
}

void KoanSpaceRouting::AddRouteForTabs(
    const std::vector<tabs::TabInterface*>& tabs) {
  std::vector<std::string> hosts;
  for (tabs::TabInterface* tab : tabs) {
    hosts.emplace_back(tab->GetContents()->GetLastCommittedURL().host());
  }
  if (std::optional<SpaceRoute> route = RouteForHosts(hosts)) {
    PrefService* prefs = PrefsOf(browser_view_);
    std::vector<SpaceRoute> routes = ReadRoutes(prefs);
    routes.push_back(std::move(*route));
    prefs->SetList(prefs::kSpaceRoutes, SpaceRoutesToValue(routes));
  }
  // Zen then shows the routes, to pick the Space. Not from inside the menu.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanSpaceRouting::ShowSettings,
                                weak_factory_.GetWeakPtr()));
}

void KoanSpaceRouting::ShowSettings() {
  if (destroying_ || !browser_view_->GetWidget()) {
    return;
  }
  PrefService* prefs = PrefsOf(browser_view_);
  std::vector<std::u16string> spaces;
  for (const KoanWorkspaceController::SpaceInfo& info :
       controller_->GetSpaces()) {
    spaces.push_back(info.name);
  }
  auto editor = std::make_unique<KoanSpaceRoutingEditor>(
      std::move(spaces), ReadRoutes(prefs),
      base::UTF8ToUTF16(prefs->GetString(prefs::kSpaceRoutingExternalDefault)));
  KoanSpaceRoutingEditor* editor_view = editor.get();
  std::unique_ptr<ui::DialogModel> model =
      ui::DialogModel::Builder(std::make_unique<ui::DialogModelDelegate>())
          .SetTitle(kTitle)
          .AddCustomField(
              std::make_unique<views::BubbleDialogModelHost::CustomView>(
                  std::move(editor),
                  views::BubbleDialogModelHost::FieldType::kControl))
          .AddOkButton(
              base::BindOnce(&SaveRoutes, prefs, editor_view->GetWeakPtr()),
              ui::DialogModel::Button::Params().SetLabel(u"Save").SetId(
                  kKoanSpaceRoutingSave))
          .AddCancelButton(base::DoNothing())
          .Build();
  // The editor lives in the dialog, as does its model.
  editor_view->SetValidityCallback(base::BindRepeating(
      [](ui::DialogModel* dialog, bool valid) {
        dialog->SetButtonEnabled(
            dialog->GetButtonByUniqueId(kKoanSpaceRoutingSave), valid);
      },
      base::Unretained(model.get())));
  constrained_window::ShowBrowserModal(std::move(model),
                                       browser_view_->GetNativeWindow());
}

}  // namespace koan
