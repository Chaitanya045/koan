// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_pinned_tabs.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/resource_coordinator/lifecycle_unit_state.mojom.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/dialogs/browser_dialogs.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_data.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/performance_manager/public/decorators/page_live_state_decorator.h"
#include "components/prefs/pref_service.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_context_menu_command.h"
#include "components/tabs/public/tab_group.h"
#include "components/url_formatter/url_fixer.h"
#include "content/public/browser/invalidate_type.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/common/referrer.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/koan_tab_hooks.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_pinned_tab_views.h"
#include "koan/browser/ui/views/koan_pinned_tabs_hooks.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_tab_menu.h"
#include "koan/browser/ui/views/koan_tab_session_data.h"
#include "koan/browser/ui/views/koan_toast.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/page_transition_types.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/view.h"

namespace koan {

// Koan's tab menu items and the Edit Pinned URL field, findable by name
// (UI DevTools "id:" search, used by the e2e test).
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanAddToEssentialsMenuItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanAddToEssentialsMenuItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanRemoveFromEssentialsMenuItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanRemoveFromEssentialsMenuItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanChangeLabelMenuItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanChangeLabelMenuItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanChangeIconMenuItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanChangeIconMenuItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanUnloadMenuItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanUnloadMenuItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanReplaceWithCurrentUrlMenuItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanReplaceWithCurrentUrlMenuItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanEditPinnedUrlMenuItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanEditPinnedUrlMenuItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanResetPinnedTabMenuItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanResetPinnedTabMenuItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanEditPinnedUrlSubmenu);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanEditPinnedUrlSubmenu);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanEditPinnedUrlField);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanEditPinnedUrlField);
// Upstream's Close Other Tabs and Close Tabs Below, tagged for the e2e test.
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanCloseOtherTabsMenuItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanCloseOtherTabsMenuItem);
DECLARE_ELEMENT_IDENTIFIER_VALUE(kKoanCloseTabsBelowMenuItem);
DEFINE_ELEMENT_IDENTIFIER_VALUE(kKoanCloseTabsBelowMenuItem);

namespace {

// Pinned tabs' tab context menu commands (koan_tab_menu.h).
enum KoanTabMenuCommand {
  kAddToEssentials = FirstTabMenuCommand(TabMenuFeatureId::kPinnedTabs),
  kRemoveFromEssentials,
  kChangeLabel,
  kChangeIcon,
  kUnload,
  kReplaceWithCurrentUrl,
  kEditPinnedUrl,
  kResetPinnedTab,
  kEditPinnedUrlSubmenu,
  kFirstKoanCommand = kAddToEssentials,
  kLastKoanCommand = kEditPinnedUrlSubmenu,
};

// Keys of the serialized KoanTabState.
constexpr char kPinnedUrlKey[] = "pinned_url";
constexpr char kPinnedInSpaceKey[] = "pinned_in_space";
constexpr char kResetPendingKey[] = "reset";
constexpr char kCustomTitleKey[] = "title";
constexpr char kCustomIconKey[] = "icon";

// Zen's close-shortcut behaviours; the last is Zen's default.
constexpr std::string_view kCloseBehaviors[] = {
    "close",        "reset",         "switch",
    "reset-switch", "unload-switch", "reset-unload-switch",
};
constexpr std::string_view kDefaultCloseBehavior = "reset-unload-switch";

// How long moves must pause before Koan re-checks the pinned tabs: a drag
// moves the tab on every step.
constexpr base::TimeDelta kNormalizeDelay = base::Milliseconds(400);

class KoanTabData : public content::WebContentsUserData<KoanTabData> {
 public:
  ~KoanTabData() override = default;

  KoanTabState& state() { return state_; }

 private:
  friend WebContentsUserData;
  explicit KoanTabData(content::WebContents* contents)
      : WebContentsUserData(*contents) {}

  KoanTabState state_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(KoanTabData);

KoanTabState& GetOrCreateTabState(content::WebContents* contents) {
  return KoanTabData::GetOrCreateForWebContents(contents)->state();
}

// The URL the tab shows: its last committed entry, which a restored tab has
// before it loads.
GURL CurrentUrl(content::WebContents* contents) {
  content::NavigationEntry* entry =
      contents->GetController().GetLastCommittedEntry();
  return entry ? entry->GetURL() : GURL();
}

// Whether the tab shows `url` left the pinned URL, for the reset button:
// like Zen, a different fragment is still the pinned page, and an empty page
// in between doesn't count as leaving it. (Resetting doesn't ignore the
// fragment.)
bool LeftPinnedUrl(const GURL& url, const GURL& pinned_url) {
  if (url.is_empty() || url.IsAboutBlank()) {
    return false;
  }
  return !url.EqualsIgnoringRef(pinned_url);
}

// Whether the tab isn't loaded: discarded, or restored on demand and not
// shown yet (Koan's notion of a pending tab, as for Ctrl+Tab).
bool IsPending(content::WebContents* contents) {
  return contents->WasDiscarded() || contents->GetController().NeedsReload();
}

std::string_view CloseBehavior(Profile* profile) {
  const std::string& behavior =
      profile->GetPrefs()->GetString(prefs::kPinnedTabCloseShortcutBehavior);
  for (std::string_view known : kCloseBehaviors) {
    if (behavior == known) {
      return known;
    }
  }
  // A value Koan doesn't know (edited by hand, or from a later Koan).
  return kDefaultCloseBehavior;
}

// The entries of a tab reset to `url`: only that one, like Zen's reset.
std::vector<std::unique_ptr<content::NavigationEntry>> PinnedUrlEntries(
    const GURL& url,
    content::BrowserContext* context) {
  std::vector<std::unique_ptr<content::NavigationEntry>> entries;
  entries.push_back(content::NavigationController::CreateNavigationEntry(
      url, content::Referrer(), /*initiator_origin=*/std::nullopt,
      /*initiator_base_url=*/std::nullopt, ui::PAGE_TRANSITION_AUTO_BOOKMARK,
      /*is_renderer_initiated=*/false, /*extra_headers=*/std::string(), context,
      /*blob_url_loader_factory=*/nullptr));
  return entries;
}

// Paints an emoji as a favicon.
class EmojiImageSource : public gfx::CanvasImageSource {
 public:
  EmojiImageSource(std::u16string emoji, SkColor color)
      : gfx::CanvasImageSource(gfx::Size(kPinnedIconSize, kPinnedIconSize)),
        emoji_(std::move(emoji)),
        color_(color) {}

  // gfx::CanvasImageSource:
  void Draw(gfx::Canvas* canvas) override {
    const gfx::FontList font_list;
    canvas->DrawStringRectWithFlags(
        emoji_,
        font_list.DeriveWithSizeDelta(kCustomIconFontSize -
                                      font_list.GetFontSize()),
        color_, gfx::Rect(size()), gfx::Canvas::TEXT_ALIGN_CENTER);
  }

 private:
  const std::u16string emoji_;
  // For emoji the font has no colour glyph of.
  const SkColor color_;
};

ui::ImageModel EmojiIcon(const std::u16string& emoji) {
  return ui::ImageModel::FromImageGenerator(
      base::BindRepeating(
          [](const std::u16string& emoji, const ui::ColorProvider* colors) {
            return gfx::CanvasImageSource::MakeImageSkia<EmojiImageSource>(
                emoji, colors ? colors->GetColor(kColorToolbarText)
                              : gfx::kPlaceholderColor);
          },
          emoji),
      gfx::Size(kPinnedIconSize, kPinnedIconSize));
}

// Pinned tabs, like Essentials, are never frozen or discarded by Memory
// Saver (both exempt the pages Chromium knows as pinned tabs).
void UpdateFreezingExemption(tabs::TabInterface* tab) {
  performance_manager::PageLiveStateDecorator::SetIsPinnedTab(
      tab->GetContents(), IsPinnedForUser(tab));
}

// Zen's toast in `browser_view`'s window.
void ShowToast(BrowserView* browser_view, const std::u16string& text) {
  if (KoanBrowserViewController* controller =
          KoanBrowserViewController::From(browser_view)) {
    controller->toast()->Show(text);
  }
}

// The tabs a context menu command on `tab` applies to: the selection if
// `tab` is part of it, like upstream's commands.
std::vector<tabs::TabInterface*> TabsForCommand(TabStripModel* model,
                                                tabs::TabInterface* tab) {
  const int index = model->GetIndexOfTab(tab);
  if (index == TabStripModel::kNoTab || !model->IsTabSelected(index)) {
    return {tab};
  }
  std::vector<tabs::TabInterface*> tabs;
  for (int i = 0; i < model->count(); ++i) {
    if (model->IsTabSelected(i)) {
      tabs.push_back(model->GetTabAtIndex(i));
    }
  }
  return tabs;
}

std::vector<tabs::TabHandle> HandlesOf(
    const std::vector<tabs::TabInterface*>& tabs) {
  std::vector<tabs::TabHandle> handles;
  for (tabs::TabInterface* tab : tabs) {
    handles.push_back(tab->GetHandle());
  }
  return handles;
}

// Inserts `command_id` with `label` at `index` and tags it for tests.
size_t InsertItem(ui::SimpleMenuModel* model,
                  size_t index,
                  int command_id,
                  const std::u16string& label,
                  ui::ElementIdentifier id) {
  model->InsertItemAt(index, command_id, label);
  model->SetElementIdentifierAt(index, id);
  return index + 1;
}

}  // namespace

// Discards a tab once the navigation back to its pinned URL is done, for
// Zen's "reset, unload and switch": the tab ends up unloaded at its pinned
// URL with a fresh history.
class KoanPinnedTabController::DiscardAfterReset
    : public content::WebContentsObserver {
 public:
  DiscardAfterReset(KoanPinnedTabController* controller,
                    content::WebContents* contents)
      : content::WebContentsObserver(contents), controller_(controller) {}

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    // Any end, committed or not (an error, a download, a 204), so the tab
    // never stays loaded.
    if (!handle->IsInPrimaryMainFrame()) {
      return;
    }
    // Never synchronously: the discard replaces the WebContents notifying us.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<KoanPinnedTabController> controller,
                          base::WeakPtr<content::WebContents> contents) {
                         if (controller && contents) {
                           controller->Discard(contents.get());
                         }
                       },
                       controller_->weak_factory_.GetWeakPtr(),
                       web_contents()->GetWeakPtr()));
  }

  void WebContentsDestroyed() override {
    controller_->discard_after_reset_.erase(web_contents());
  }

 private:
  const raw_ptr<KoanPinnedTabController> controller_;
};

KoanTabState::KoanTabState() = default;
KoanTabState::KoanTabState(const KoanTabState&) = default;
KoanTabState& KoanTabState::operator=(const KoanTabState&) = default;
KoanTabState::~KoanTabState() = default;

bool KoanTabState::empty() const {
  return pinned_url.is_empty() && !pinned_in_space && custom_title.empty() &&
         custom_icon.empty();
}

std::string KoanTabState::Serialize() const {
  if (empty()) {
    return std::string();
  }
  base::DictValue dict;
  if (pinned_url.is_valid()) {
    dict.Set(kPinnedUrlKey, pinned_url.spec());
  }
  if (pinned_in_space) {
    dict.Set(kPinnedInSpaceKey, true);
  }
  if (reset_pending) {
    dict.Set(kResetPendingKey, true);
  }
  if (!custom_title.empty()) {
    dict.Set(kCustomTitleKey, custom_title);
  }
  if (!custom_icon.empty()) {
    dict.Set(kCustomIconKey, custom_icon);
  }
  std::string json;
  base::JSONWriter::Write(dict, &json);
  return json;
}

// static
KoanTabState KoanTabState::Deserialize(std::string_view json) {
  KoanTabState state;
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!dict) {
    return state;
  }
  if (const std::string* url = dict->FindString(kPinnedUrlKey)) {
    state.pinned_url = GURL(*url);
  }
  state.pinned_in_space = dict->FindBool(kPinnedInSpaceKey).value_or(false) &&
                          state.pinned_url.is_valid();
  state.reset_pending = dict->FindBool(kResetPendingKey).value_or(false) &&
                        state.pinned_url.is_valid();
  if (const std::string* title = dict->FindString(kCustomTitleKey)) {
    state.custom_title = base::UTF8ToUTF16(*title);
  }
  if (const std::string* icon = dict->FindString(kCustomIconKey)) {
    state.custom_icon = base::UTF8ToUTF16(*icon);
  }
  return state;
}

KoanTabState* GetTabState(content::WebContents* contents) {
  KoanTabData* data =
      contents ? KoanTabData::FromWebContents(contents) : nullptr;
  return data ? &data->state() : nullptr;
}

KoanTabState* GetTabState(const tabs::TabInterface* tab) {
  return tab ? GetTabState(tab->GetContents()) : nullptr;
}

bool IsPinnedInSpace(const tabs::TabInterface* tab) {
  const KoanTabState* state = GetTabState(tab);
  return state && state->pinned_in_space && !tab->IsPinned();
}

bool IsPinnedForUser(const tabs::TabInterface* tab) {
  return tab && (tab->IsPinned() ||
                 (features::IsPinnedTabsEnabled() && IsPinnedInSpace(tab)));
}

bool IsPinnedForUser(content::WebContents* contents) {
  return contents &&
         IsPinnedForUser(tabs::TabInterface::MaybeGetFromContents(contents));
}

bool IsAwayFromPinnedUrl(const tabs::TabInterface* tab) {
  const KoanTabState* state = GetTabState(tab);
  return IsPinnedForUser(tab) && state && state->pinned_url.is_valid() &&
         !state->reset_pending &&
         LeftPinnedUrl(CurrentUrl(tab->GetContents()), state->pinned_url);
}

bool IsPendingTab(const tabs::TabInterface* tab) {
  return tab && IsPending(tab->GetContents());
}

KoanPinnedTabController::KoanPinnedTabController(BrowserView* browser_view)
    : browser_view_(browser_view) {
  if (features::IsPinnedTabsEnabled()) {
    tab_strip_model()->AddObserver(this);
  }
}

KoanPinnedTabController::~KoanPinnedTabController() = default;

// static
KoanPinnedTabController* KoanPinnedTabController::ForTab(
    const tabs::TabInterface* tab) {
  if (!tab || !features::IsPinnedTabsEnabled()) {
    return nullptr;
  }
  const BrowserWindowInterface* browser = tab->GetBrowserWindowInterface();
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  return controller ? controller->pinned_tabs() : nullptr;
}

void KoanPinnedTabController::OnBrowserViewDestroying() {
  // Unconditional: removing an observer that was never added is a no-op.
  tab_strip_model()->RemoveObserver(this);
  discard_after_reset_.clear();
  normalize_timer_.Stop();
  favicon_tracker_.TryCancelAll();
}

TabStripModel* KoanPinnedTabController::tab_strip_model() const {
  return browser_view_->browser()->GetTabStripModel();
}

int KoanPinnedTabController::EssentialsCount() const {
  return tab_strip_model()->IndexOfFirstNonPinnedTab();
}

bool KoanPinnedTabController::CanAddToEssentials() const {
  return EssentialsCount() < kMaxEssentials;
}

void KoanPinnedTabController::InitPinnedState(tabs::TabInterface* tab,
                                              KoanTabState& state) {
  if (state.pinned_url.is_valid()) {
    return;
  }
  content::WebContents* contents = tab->GetContents();
  content::NavigationEntry* entry =
      contents->GetController().GetLastCommittedEntry();
  // A tab that hasn't shown its first page yet: the page it is opening.
  state.pinned_url = entry && !entry->IsInitialEntry()
                         ? entry->GetURL()
                         : contents->GetVisibleURL();
  state.pinned_icon = TabUIHelper::From(tab)->GetFavicon();
}

void KoanPinnedTabController::FetchPinnedIcon(content::WebContents* contents) {
  KoanTabState* state = GetTabState(contents);
  favicon::FaviconService* favicons = FaviconServiceFactory::GetForProfile(
      browser_view_->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
  if (!state || !favicons) {
    return;
  }
  // Once per pinned URL: the database may not have an icon for it.
  state->pinned_icon_requested = state->pinned_url;
  favicons->GetFaviconImageForPageURL(
      state->pinned_url,
      base::BindOnce(
          [](base::WeakPtr<content::WebContents> contents, const GURL& url,
             const favicon_base::FaviconImageResult& result) {
            KoanTabState* state =
                contents ? GetTabState(contents.get()) : nullptr;
            if (!state || state->pinned_url != url || result.image.IsEmpty()) {
              return;
            }
            state->pinned_icon = ui::ImageModel::FromImage(result.image);
            UpdateTabRow(
                tabs::TabInterface::MaybeGetFromContents(contents.get()));
          },
          contents->GetWeakPtr(), state->pinned_url),
      &favicon_tracker_);
}

std::vector<tabs::TabInterface*> KoanPinnedTabController::WithSplit(
    tabs::TabInterface* tab) const {
  if (std::optional<split_tabs::SplitTabId> split = tab->GetSplit()) {
    if (split_tabs::SplitTabData* data =
            tab_strip_model()->GetSplitData(*split)) {
      return data->ListTabs();
    }
  }
  return {tab};
}

int KoanPinnedTabController::SectionStart(int index) const {
  TabStripModel* model = tab_strip_model();
  const std::optional<tab_groups::TabGroupId> group =
      model->GetTabGroupForTab(index);
  return group ? static_cast<int>(model->group_model()
                                      ->GetTabGroup(*group)
                                      ->ListTabs()
                                      .start())
               : model->IndexOfFirstNonPinnedTab();
}

int KoanPinnedTabController::LastPinnedInSection(
    int index,
    const std::vector<tabs::TabInterface*>& ignore) const {
  TabStripModel* model = tab_strip_model();
  const std::optional<tab_groups::TabGroupId> group =
      model->GetTabGroupForTab(index);
  int last = -1;
  for (int i = SectionStart(index); i < model->count(); ++i) {
    tabs::TabInterface* tab = model->GetTabAtIndex(i);
    if (std::ranges::contains(ignore, tab)) {
      continue;
    }
    if (tab->IsPinned() || model->GetTabGroupForTab(i) != group ||
        !IsPinnedInSpace(tab)) {
      break;
    }
    last = i;
  }
  return last;
}

bool KoanPinnedTabController::InPinnedRun(int index) const {
  TabStripModel* model = tab_strip_model();
  for (int i = SectionStart(index); i < index; ++i) {
    if (!IsPinnedInSpace(model->GetTabAtIndex(i))) {
      return false;
    }
  }
  return true;
}

bool KoanPinnedTabController::AbovePinnedTab(int index) const {
  TabStripModel* model = tab_strip_model();
  const std::optional<tab_groups::TabGroupId> group =
      model->GetTabGroupForTab(index);
  for (int i = index + 1; i < model->count(); ++i) {
    if (model->IsTabPinned(i) || model->GetTabGroupForTab(i) != group) {
      return false;
    }
    if (IsPinnedInSpace(model->GetTabAtIndex(i))) {
      return true;
    }
  }
  return false;
}

void KoanPinnedTabController::MoveToPinnedSection(tabs::TabInterface* tab) {
  TabStripModel* model = tab_strip_model();
  const std::vector<tabs::TabInterface*> tabs = WithSplit(tab);
  const int index = model->GetIndexOfTab(tabs.front());
  if (index == TabStripModel::kNoTab) {
    return;
  }
  const int last = LastPinnedInSection(index, tabs);
  const int target = last == -1 ? SectionStart(index) : last + 1;
  if (target >= index) {
    return;
  }
  // A split view moves as one, or Chromium would take it apart.
  if (std::optional<split_tabs::SplitTabId> split = tab->GetSplit()) {
    model->MoveSplitTo(*split, target, /*pinned=*/false,
                       model->GetTabGroupForTab(index));
  } else {
    model->MoveWebContentsAt(index, target, /*select_after_move=*/false);
  }
}

void KoanPinnedTabController::OnTabsAdded(std::vector<tabs::TabHandle> tabs) {
  TabStripModel* model = tab_strip_model();
  for (const tabs::TabHandle& handle : tabs) {
    tabs::TabInterface* tab = handle.Get();
    const int index = tab ? model->GetIndexOfTab(tab) : TabStripModel::kNoTab;
    if (index == TabStripModel::kNoTab) {
      continue;
    }
    // After upstream marked the page as (not) pinned for the freezing policy.
    if (IsPinnedInSpace(tab)) {
      UpdateFreezingExemption(tab);
    }
    if (IsPinnedForUser(tab) || tab->IsSplit()) {
      continue;
    }
    const int last = LastPinnedInSection(index, {tab});
    if (last > index) {
      model->MoveWebContentsAt(index, last, /*select_after_move=*/false);
    }
  }
}

void KoanPinnedTabController::ScheduleNormalize(tabs::TabInterface* tab) {
  to_normalize_.push_back(tab->GetHandle());
  normalize_timer_.Start(FROM_HERE, kNormalizeDelay, this,
                         &KoanPinnedTabController::Normalize);
}

void KoanPinnedTabController::Normalize() {
  TabStripModel* model = tab_strip_model();
  const std::vector<tabs::TabHandle> handles = std::move(to_normalize_);
  to_normalize_.clear();
  for (const tabs::TabHandle& handle : handles) {
    tabs::TabInterface* tab = handle.Get();
    const int index = tab ? model->GetIndexOfTab(tab) : TabStripModel::kNoTab;
    if (index == TabStripModel::kNoTab) {
      continue;
    }
    KoanTabState& state = GetOrCreateTabState(tab->GetContents());
    if (tab->IsPinned()) {
      // Dragged into the tiles, or duplicated from one, past Zen's limit:
      // back into the Space. (Not while a session restores what it had.)
      if (EssentialsCount() > kMaxEssentials &&
          !SessionRestore::IsRestoring(browser_view_->GetProfile())) {
        RemoveFromEssentials(tab);
      }
      continue;
    }
    if (std::exchange(state.was_essential, false)) {
      // Zen: an Essential dropped among the pinned tabs becomes one of them,
      // keeping its pinned URL; dropped anywhere else, a normal tab.
      if (InPinnedRun(index) && state.pinned_url.is_valid()) {
        state.pinned_in_space = true;
        UpdateFreezingExemption(tab);
        SaveTabState(tab->GetContents());
        UpdateTabView(tab);
      } else {
        ClearPinnedState(tab);
      }
      continue;
    }
    if (std::exchange(state.changed_group, false) && IsPinnedInSpace(tab)) {
      // Moved to another Space: still pinned, at the top of that one.
      MoveToPinnedSection(tab);
      continue;
    }
    // Zen's drop rules for a drag within the Space.
    if (IsPinnedInSpace(tab) && !InPinnedRun(index)) {
      ClearPinnedState(tab);
    } else if (!IsPinnedForUser(tab) && AbovePinnedTab(index)) {
      PinInSpace(tab);
    }
  }
}

void KoanPinnedTabController::ClearPinnedState(tabs::TabInterface* tab) {
  KoanTabState& state = GetOrCreateTabState(tab->GetContents());
  state.pinned_in_space = false;
  state.reset_pending = false;
  state.pinned_url = GURL();
  state.pinned_icon = ui::ImageModel();
  UpdateFreezingExemption(tab);
  SaveTabState(tab->GetContents());
  UpdateTabView(tab);
}

void KoanPinnedTabController::TogglePin(tabs::TabInterface* tab) {
  if (tab->IsPinned()) {
    RemoveFromEssentials(tab);
  } else if (IsPinnedInSpace(tab)) {
    UnpinFromSpace(tab);
  } else {
    PinInSpace(tab);
  }
}

void KoanPinnedTabController::PinInSpace(tabs::TabInterface* tab) {
  TabStripModel* model = tab_strip_model();
  if (tab->IsPinned()) {
    // An Essential moves to the pinned tabs of the active Space, keeping its
    // pinned URL (Zen's removeEssentials without unpinning).
    base::AutoReset<bool> changing(&changing_pin_, true);
    model->SetTabPinned(model->GetIndexOfTab(tab), false);
    if (std::optional<tab_groups::TabGroupId> space =
            model->GetFocusedGroup()) {
      model->AddToExistingGroup({model->GetIndexOfTab(tab)}, *space);
    }
  }
  for (tabs::TabInterface* each : WithSplit(tab)) {
    KoanTabState& state = GetOrCreateTabState(each->GetContents());
    InitPinnedState(each, state);
    state.pinned_in_space = true;
  }
  MoveToPinnedSection(tab);
  for (tabs::TabInterface* each : WithSplit(tab)) {
    UpdateFreezingExemption(each);
    SaveTabState(each->GetContents());
    UpdateTabView(each);
  }
}

void KoanPinnedTabController::UnpinFromSpace(tabs::TabInterface* tab) {
  if (!IsPinnedInSpace(tab)) {
    return;
  }
  const std::vector<tabs::TabInterface*> tabs = WithSplit(tab);
  for (tabs::TabInterface* each : tabs) {
    ClearPinnedState(each);
  }
  // Zen moves an unpinned tab to the top of the Space's other tabs.
  TabStripModel* model = tab_strip_model();
  const int index = model->GetIndexOfTab(tab);
  const int last = LastPinnedInSection(index, tabs);
  if (!tab->IsSplit() && last > index) {
    model->MoveWebContentsAt(index, last, /*select_after_move=*/false);
  }
}

void KoanPinnedTabController::AddToEssentials(tabs::TabInterface* tab) {
  if (tab->IsPinned() || !CanAddToEssentials()) {
    return;
  }
  KoanTabState& state = GetOrCreateTabState(tab->GetContents());
  // A pinned tab keeps its pinned URL; any other tab is pinned on its
  // current one.
  state.pinned_in_space = false;
  InitPinnedState(tab, state);
  base::AutoReset<bool> changing(&changing_pin_, true);
  tab_strip_model()->SetTabPinned(tab_strip_model()->GetIndexOfTab(tab), true);
  SaveTabState(tab->GetContents());
  UpdateTabRow(tab);
}

void KoanPinnedTabController::RemoveFromEssentials(tabs::TabInterface* tab) {
  if (!tab->IsPinned()) {
    return;
  }
  TabStripModel* model = tab_strip_model();
  {
    base::AutoReset<bool> changing(&changing_pin_, true);
    model->SetTabPinned(model->GetIndexOfTab(tab), false);
    if (std::optional<tab_groups::TabGroupId> space =
            model->GetFocusedGroup()) {
      model->AddToExistingGroup({model->GetIndexOfTab(tab)}, *space);
    }
  }
  ClearPinnedState(tab);
  // A normal tab: below the Space's pinned tabs.
  const int index = model->GetIndexOfTab(tab);
  const int last = LastPinnedInSection(index, {tab});
  if (last > index) {
    model->MoveWebContentsAt(index, last, /*select_after_move=*/false);
  }
}

void KoanPinnedTabController::ResetToPinnedUrl(tabs::TabInterface* tab) {
  KoanTabState* state = GetTabState(tab);
  if (!IsPinnedForUser(tab) || !state || !state->pinned_url.is_valid()) {
    return;
  }
  content::WebContents* contents = tab->GetContents();
  if (IsPending(contents) && !tab->IsActivated()) {
    // Like Zen, an unloaded tab stays unloaded: it goes to the pinned URL
    // when it is shown.
    state->reset_pending = true;
    SaveTabState(contents);
    UpdateTabRow(tab);
    return;
  }
  state->reset_pending = false;
  content::NavigationController::LoadURLParams params(state->pinned_url);
  params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
  // Zen keeps only the pinned entry in the tab's history.
  params.should_clear_history_list = true;
  contents->GetController().LoadURLWithParams(params);
}

void KoanPinnedTabController::OnResetButtonPressed(tabs::TabInterface* tab,
                                                   bool separate) {
  TabStripModel* model = tab_strip_model();
  if (separate) {
    chrome::DuplicateTabAt(browser_view_->browser(), model->GetIndexOfTab(tab));
  }
  model->ActivateTabAt(model->GetIndexOfTab(tab));
  ResetToPinnedUrl(tab);
}

void KoanPinnedTabController::ReplacePinnedUrlWithCurrent(
    tabs::TabInterface* tab) {
  KoanTabState* state = GetTabState(tab);
  if (!IsPinnedForUser(tab) || !state) {
    return;
  }
  state->pinned_url = CurrentUrl(tab->GetContents());
  state->reset_pending = false;
  state->pinned_icon = TabUIHelper::From(tab)->GetFavicon();
  SaveTabState(tab->GetContents());
  UpdateTabView(tab);
  ShowToast(browser_view_,
            u"Pinned tab URL has been replaced with the current URL!");
}

void KoanPinnedTabController::EditPinnedUrl(tabs::TabInterface* tab) {
  KoanTabState* state = GetTabState(tab);
  if (!IsPinnedForUser(tab) || !state) {
    return;
  }
  const GURL initial = state->pinned_url.is_valid()
                           ? state->pinned_url
                           : CurrentUrl(tab->GetContents());
  auto model_builder = std::make_unique<ui::DialogModel::Builder>();
  ui::DialogModel* model = model_builder->model();
  model_builder->SetTitle(u"Edit Pinned URL")
      .AddParagraph(ui::DialogModelLabel(
          u"Enter the URL this pinned tab should point to:"))
      .AddTextfield(kKoanEditPinnedUrlField, std::u16string(),
                    base::UTF8ToUTF16(initial.spec()))
      .SetInitiallyFocusedField(kKoanEditPinnedUrlField)
      .AddOkButton(base::BindOnce(
          [](base::WeakPtr<KoanPinnedTabController> controller,
             tabs::TabHandle handle, GURL initial, ui::DialogModel* model) {
            tabs::TabInterface* tab = handle.Get();
            if (!controller || !tab) {
              return;
            }
            std::u16string text;
            base::TrimWhitespace(
                model->GetTextfieldByUniqueId(kKoanEditPinnedUrlField)->text(),
                base::TRIM_ALL, &text);
            const GURL url =
                url_formatter::FixupURL(base::UTF16ToUTF8(text), std::string());
            if (!url.is_valid()) {
              ShowToast(controller->browser_view_,
                        u"That doesn't look like a valid URL.");
              return;
            }
            // Like Zen, an unchanged URL changes nothing.
            if (url != initial) {
              controller->SetPinnedUrl(tab, url);
              ShowToast(controller->browser_view_,
                        u"Pinned tab URL has been updated!");
            }
          },
          weak_factory_.GetWeakPtr(), tab->GetHandle(), initial,
          base::Unretained(model)))
      .AddCancelButton(base::DoNothing());
  chrome::ShowBrowserModal(browser_view_->browser(), model_builder->Build());
}

void KoanPinnedTabController::SetPinnedUrl(tabs::TabInterface* tab,
                                           const GURL& url) {
  KoanTabState* state = GetTabState(tab);
  if (!IsPinnedForUser(tab) || !state) {
    return;
  }
  state->pinned_url = url;
  state->pinned_icon = ui::ImageModel();
  FetchPinnedIcon(tab->GetContents());
  SaveTabState(tab->GetContents());
  ResetToPinnedUrl(tab);
}

void KoanPinnedTabController::StartRenaming(tabs::TabInterface* tab) {
  StartRenamingTabRow(tab);
}

void KoanPinnedTabController::SetCustomTitle(tabs::TabInterface* tab,
                                             std::u16string title) {
  // Zen collapses runs of whitespace in a new label.
  title = base::CollapseWhitespace(title, /*trim_sequences_with_line_breaks=*/
                                   false);
  base::TrimWhitespace(title, base::TRIM_ALL, &title);
  const bool renamed = !title.empty();
  GetOrCreateTabState(tab->GetContents()).custom_title = std::move(title);
  SaveTabState(tab->GetContents());
  UpdateTabView(tab);
  if (renamed) {
    ShowToast(browser_view_, u"Tab has been successfully renamed!");
  }
}

void KoanPinnedTabController::ShowIconPicker(tabs::TabInterface* tab) {
  views::View* anchor = GetTabRowView(tab);
  ShowTabIconPicker(anchor ? anchor : browser_view_->tab_strip_view(), tab);
}

void KoanPinnedTabController::SetCustomIcon(tabs::TabInterface* tab,
                                            std::u16string icon) {
  GetOrCreateTabState(tab->GetContents()).custom_icon = std::move(icon);
  SaveTabState(tab->GetContents());
  UpdateTabView(tab);
}

void KoanPinnedTabController::SelectAnotherTab(tabs::TabInterface* tab) {
  TabStripModel* model = tab_strip_model();
  const int index = model->GetIndexOfTab(tab);
  if (index == TabStripModel::kNoTab || index != model->active_index()) {
    return;
  }
  // The tabs on show: the Essentials and the active Space's tabs.
  const std::optional<tab_groups::TabGroupId> space = model->GetFocusedGroup();
  const auto shown = [&](int i) {
    return i != index && (model->IsTabPinned(i) || !space ||
                          model->GetTabGroupForTab(i) == space);
  };
  // Like closing a tab (Firefox's and Zen's rules): the tab that opened this
  // one, else the most recently used one (Tab Management setting), else the
  // next tab, else the previous one.
  if (tabs::TabInterface* opener = model->GetOpenerOfTabAt(index)) {
    const int opener_index = model->GetIndexOfTab(opener);
    if (opener_index != TabStripModel::kNoTab && shown(opener_index)) {
      model->ActivateTabAt(opener_index);
      return;
    }
  }
  if (std::optional<int> recent = PickRecentlyUsedTab(
          browser_view_->GetProfile(), model->count(),
          [&](int i) -> content::WebContents* {
            return shown(i) ? model->GetWebContentsAt(i) : nullptr;
          })) {
    model->ActivateTabAt(*recent);
    return;
  }
  for (int i = index + 1; i < model->count(); ++i) {
    if (shown(i)) {
      model->ActivateTabAt(i);
      return;
    }
  }
  for (int i = index - 1; i >= 0; --i) {
    if (shown(i)) {
      model->ActivateTabAt(i);
      return;
    }
  }
  chrome::AddTabAt(browser_view_->browser(), GURL(), model->count(),
                   /*foreground=*/true, space);
}

void KoanPinnedTabController::Discard(content::WebContents* contents) {
  discard_after_reset_.erase(contents);
  TabStripModel* model = tab_strip_model();
  const int index = model->GetIndexOfWebContents(contents);
  // Not if it went away, isn't loaded anyway or was selected meanwhile.
  if (index == TabStripModel::kNoTab || index == model->active_index() ||
      IsPending(contents)) {
    return;
  }
  if (auto* lifecycle =
          resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
              contents)) {
    lifecycle->DiscardTab(::mojom::LifecycleUnitDiscardReason::EXTERNAL);
  }
}

void KoanPinnedTabController::UnloadTabs(
    const std::vector<tabs::TabInterface*>& tabs) {
  TabStripModel* model = tab_strip_model();
  const std::vector<tabs::TabHandle> handles = HandlesOf(tabs);
  for (const tabs::TabHandle& handle : handles) {
    tabs::TabInterface* tab = handle.Get();
    if (tab && model->GetIndexOfTab(tab) == model->active_index()) {
      SelectAnotherTab(tab);
    }
  }
  for (const tabs::TabHandle& handle : handles) {
    if (tabs::TabInterface* tab = handle.Get()) {
      Discard(tab->GetContents());
    }
  }
}

void KoanPinnedTabController::RunCloseBehaviour(tabs::TabInterface* tab,
                                                std::string_view behavior) {
  TabStripModel* model = tab_strip_model();
  if (behavior == "close") {
    // Into the history, so Ctrl+Shift+T brings it back.
    model->CloseWebContentsAt(model->GetIndexOfTab(tab),
                              TabCloseTypes::CLOSE_USER_GESTURE |
                                  TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
    return;
  }
  // "reset", "switch", "reset-switch", "unload-switch" or
  // "reset-unload-switch".
  const bool reset = behavior.find("reset") != std::string_view::npos;
  const bool unload = behavior.find("unload") != std::string_view::npos;
  if (behavior.find("switch") != std::string_view::npos) {
    SelectAnotherTab(tab);
  }
  content::WebContents* contents = tab->GetContents();
  const KoanTabState* state = GetTabState(tab);
  // Like Zen, reset whatever the tab shows, a different fragment included.
  // Only a loaded tab exactly at its pinned URL that is about to be unloaded
  // is left as it is (resetting it would load the same page first).
  const bool at_pinned_url = state && CurrentUrl(contents) == state->pinned_url;
  if (reset && !(unload && at_pinned_url)) {
    const bool pending = IsPending(contents);
    ResetToPinnedUrl(tab);
    // Watch from here on, so a navigation the reset cancelled doesn't count.
    if (unload && !pending && !tab->IsActivated()) {
      discard_after_reset_[contents] =
          std::make_unique<DiscardAfterReset>(this, contents);
    }
    return;
  }
  if (unload) {
    Discard(contents);
  }
}

bool KoanPinnedTabController::HandleCloseShortcut(tabs::TabInterface* tab) {
  // Zen runs the behaviour for the selected pinned tabs, and closes nothing
  // if any is pinned.
  std::vector<tabs::TabInterface*> pinned;
  for (tabs::TabInterface* each : TabsForCommand(tab_strip_model(), tab)) {
    if (IsPinnedForUser(each)) {
      pinned.push_back(each);
    }
  }
  if (pinned.empty()) {
    return false;
  }
  const std::string_view behavior = CloseBehavior(browser_view_->GetProfile());
  // The active one last, so switching away doesn't skip the others.
  std::ranges::stable_partition(
      pinned, [](tabs::TabInterface* each) { return !each->IsActivated(); });
  for (const tabs::TabHandle& handle : HandlesOf(pinned)) {
    if (tabs::TabInterface* each = handle.Get()) {
      RunCloseBehaviour(each, behavior);
    }
  }
  return true;
}

bool KoanPinnedTabController::HandleCloseRequest(tabs::TabInterface* tab) {
  // Like Zen, a pinned tab (not an Essential) that isn't loaded closes.
  if (!IsPinnedForUser(tab) ||
      (!tab->IsPinned() && IsPending(tab->GetContents()))) {
    return false;
  }
  RunCloseBehaviour(tab, CloseBehavior(browser_view_->GetProfile()));
  return true;
}

void KoanPinnedTabController::SaveTabState(content::WebContents* contents) {
  const KoanTabState* state = GetTabState(contents);
  SetTabSessionData(contents, kPinnedTabSessionKey,
                    state ? state->Serialize() : std::string());
}

void KoanPinnedTabController::UpdateTabView(tabs::TabInterface* tab) {
  UpdateTabRow(tab);
  // The label and icon come from the tab's data (AdjustTabData()).
  tab->GetContents()->NotifyNavigationStateChanged(
      content::INVALIDATE_TYPE_TAB);
}

void KoanPinnedTabController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  switch (change.type()) {
    case TabStripModelChange::kInserted: {
      std::vector<tabs::TabHandle> added;
      for (const auto& inserted : change.GetInsert()->contents) {
        tabs::TabInterface* tab = inserted.tab;
        if (tab->IsPinned()) {
          InitPinnedState(tab, GetOrCreateTabState(tab->GetContents()));
          // A duplicated Essential may be one too many.
          if (!CanAddToEssentials()) {
            ScheduleNormalize(tab);
          }
        }
        added.push_back(tab->GetHandle());
        UpdateTabRow(tab);
      }
      // The model can't be changed from its own notification. Posted after
      // KoanWorkspaceController's, which adds new tabs to the active Space.
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&KoanPinnedTabController::OnTabsAdded,
                         weak_factory_.GetWeakPtr(), std::move(added)));
      break;
    }
    case TabStripModelChange::kMoved:
      ScheduleNormalize(change.GetMove()->tab);
      break;
    case TabStripModelChange::kReplaced: {
      // A discard swaps the WebContents: carry Koan's state over.
      const TabStripModelChange::Replace* replace = change.GetReplace();
      if (KoanTabState* state = GetTabState(replace->old_contents)) {
        GetOrCreateTabState(replace->new_contents) = *state;
        SaveTabState(replace->new_contents);
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindOnce(&KoanPinnedTabController::OnTabsAdded,
                                      weak_factory_.GetWeakPtr(),
                                      std::vector<tabs::TabHandle>{
                                          replace->tab->GetHandle()}));
      }
      discard_after_reset_.erase(replace->old_contents);
      break;
    }
    default:
      break;
  }
  if (selection.active_tab_changed() && selection.new_tab) {
    content::WebContents* contents = selection.new_tab->GetContents();
    // Selecting a tab cancels a pending unload...
    discard_after_reset_.erase(contents);
    // ...and does a reset asked for while it wasn't loaded, before the tab
    // loads what it showed (loading clears the tab's own reload).
    if (KoanTabState* state = GetTabState(contents);
        state && state->reset_pending) {
      ResetToPinnedUrl(selection.new_tab);
    }
  }
}

void KoanPinnedTabController::OnTabChangedAt(tabs::TabInterface* tab,
                                             TabChangeType change_type) {
  content::WebContents* contents = tab->GetContents();
  KoanTabState* state = GetTabState(contents);
  if (!state || !IsPinnedForUser(tab)) {
    return;
  }
  // Keep the pinned page's icon up to date while the tab shows it.
  if (!IsAwayFromPinnedUrl(tab) && !contents->IsLoading() &&
      !IsPending(contents)) {
    state->pinned_icon = TabUIHelper::From(tab)->GetFavicon();
  } else if (state->pinned_icon.IsEmpty() &&
             state->pinned_icon_requested != state->pinned_url) {
    FetchPinnedIcon(contents);
  }
  UpdateTabRow(tab);
}

void KoanPinnedTabController::OnTabPinnedStateChanged(tabs::TabInterface* tab,
                                                      int index) {
  if (changing_pin_) {
    return;
  }
  // Dragged into or out of the tiles.
  KoanTabState& state = GetOrCreateTabState(tab->GetContents());
  if (tab->IsPinned()) {
    // A new Essential: keeps the pinned URL it had as a pinned tab.
    state.pinned_in_space = false;
    InitPinnedState(tab, state);
    SaveTabState(tab->GetContents());
    UpdateTabRow(tab);
  } else {
    // Out of the Essentials: decided where it is dropped.
    state.was_essential = true;
  }
  ScheduleNormalize(tab);
}

void KoanPinnedTabController::TabGroupedStateChanged(
    TabStripModel* tab_strip_model,
    std::optional<tab_groups::TabGroupId> old_group,
    std::optional<tab_groups::TabGroupId> new_group,
    tabs::TabInterface* tab,
    int index) {
  if (new_group && old_group && IsPinnedInSpace(tab)) {
    GetOrCreateTabState(tab->GetContents()).changed_group = true;
    ScheduleNormalize(tab);
  }
}

// --- Tab context menu ---

void AddPinnedTabMenuItems(TabMenuContext& context) {
  ui::SimpleMenuModel* model = &*context.menu;
  TabStripModel* tab_strip = &*context.tab_strip;
  const int index = context.index;
  tabs::TabInterface* tab = tab_strip->GetTabAtIndex(index);
  KoanPinnedTabController* controller = KoanPinnedTabController::ForTab(tab);
  if (!controller) {
    return;
  }
  const bool essential = tab->IsPinned();
  const bool pinned = IsPinnedForUser(tab);

  // Before upstream's Pin item, as Zen does before Firefox's: the
  // Essentials, then the label and icon.
  std::optional<size_t> pin_index =
      model->GetIndexOfCommandId(TabStripModel::CommandTogglePinned);
  if (pin_index) {
    size_t at = *pin_index;
    if (essential) {
      at = InsertItem(model, at, kRemoveFromEssentials,
                      u"Remove from Essentials",
                      kKoanRemoveFromEssentialsMenuItem);
    } else {
      at = InsertItem(model, at, kAddToEssentials, u"Add to Essentials",
                      kKoanAddToEssentialsMenuItem);
      // Zen's badge: how many of the 12 Essentials are used.
      model->SetMinorText(
          at - 1,
          base::StrCat({base::NumberToString16(controller->EssentialsCount()),
                        u" / ",
                        base::NumberToString16(
                            KoanPinnedTabController::kMaxEssentials)}));
    }
    model->InsertSeparatorAt(at++, ui::NORMAL_SEPARATOR);
    // Essentials show no label.
    if (!essential && CanRenameTabRow(tab)) {
      at = InsertItem(model, at, kChangeLabel, u"Change Label…",
                      kKoanChangeLabelMenuItem);
    }
    at = InsertItem(model, at, kChangeIcon, u"Change Icon…",
                    kKoanChangeIconMenuItem);
    model->InsertSeparatorAt(at++, ui::NORMAL_SEPARATOR);

    // Pin / Unpin: Zen's pinned tabs stay in their Space; an Essential leaves
    // through "Remove from Essentials".
    pin_index = model->GetIndexOfCommandId(TabStripModel::CommandTogglePinned);
    if (essential) {
      model->SetVisibleAt(*pin_index, false);
    } else {
      model->SetLabel(*pin_index, l10n_util::GetStringUTF16(
                                      pinned ? IDS_TAB_CXMENU_UNPIN_TAB
                                             : IDS_TAB_CXMENU_PIN_TAB));
    }
    // Firefox's "Unload" follows Pin.
    InsertItem(model, *pin_index + 1, kUnload, u"Unload", kKoanUnloadMenuItem);
  }

  // Zen hides "Close Tab" for Essentials.
  if (essential) {
    if (std::optional<size_t> close_index =
            model->GetIndexOfCommandId(TabStripModel::CommandCloseTab)) {
      model->SetVisibleAt(*close_index, false);
    }
  }
  // Upstream's (they leave pinned tabs open, see IsPinnedForUser()).
  if (std::optional<size_t> close_others =
          model->GetIndexOfCommandId(TabStripModel::CommandCloseOtherTabs)) {
    model->SetElementIdentifierAt(*close_others, kKoanCloseOtherTabsMenuItem);
  }
  if (std::optional<size_t> close_below =
          model->GetIndexOfCommandId(TabStripModel::CommandCloseTabsToRight)) {
    model->SetElementIdentifierAt(*close_below, kKoanCloseTabsBelowMenuItem);
  }

  // At the end, for one pinned tab: Zen's "Edit Pinned URL" submenu and
  // "Reset Pinned Tab".
  if (pinned && (!tab_strip->IsTabSelected(index) ||
                 tab_strip->selection_model().size() == 1)) {
    model->AddSeparator(ui::NORMAL_SEPARATOR);
    ui::SimpleMenuModel* edit = context.AddSubmenu();
    InsertItem(edit, 0, kReplaceWithCurrentUrl, u"Replace with Current URL",
               kKoanReplaceWithCurrentUrlMenuItem);
    InsertItem(edit, 1, kEditPinnedUrl, u"Edit…", kKoanEditPinnedUrlMenuItem);
    model->AddSubMenu(kEditPinnedUrlSubmenu,
                      essential ? u"Edit Essential URL" : u"Edit Pinned URL",
                      edit);
    model->SetElementIdentifierAt(model->GetItemCount() - 1,
                                  kKoanEditPinnedUrlSubmenu);
    InsertItem(model, model->GetItemCount(), kResetPinnedTab,
               essential ? u"Reset Essential Tab" : u"Reset Pinned Tab",
               kKoanResetPinnedTabMenuItem);
  }
}

bool IsPinnedTabMenuCommandEnabled(const TabStripModel* tab_strip,
                                   int index,
                                   int command_id) {
  tabs::TabInterface* tab = tab_strip->GetTabAtIndex(index);
  KoanPinnedTabController* controller = KoanPinnedTabController::ForTab(tab);
  if (!controller) {
    return false;
  }
  switch (command_id) {
    case kAddToEssentials:
      return controller->CanAddToEssentials();
    case kUnload:
      return !IsPending(tab->GetContents());
    default:
      return true;
  }
}

bool ExecutePinnedTabMenuCommand(TabStripModel* tab_strip,
                                 int index,
                                 int command_id) {
  const bool ours =
      command_id >= kFirstKoanCommand && command_id <= kLastKoanCommand;
  if (!ours && command_id != TabStripModel::CommandTogglePinned) {
    return false;
  }
  tabs::TabInterface* tab = tab_strip->GetTabAtIndex(index);
  KoanPinnedTabController* controller = KoanPinnedTabController::ForTab(tab);
  if (!controller) {
    return false;
  }
  const std::vector<tabs::TabInterface*> tabs =
      TabsForCommand(controller->tab_strip_model(), tab);
  std::vector<tabs::TabHandle> handles;
  for (tabs::TabInterface* each : tabs) {
    handles.push_back(each->GetHandle());
  }
  // Runs `action` on every tab the command applies to that still exists.
  const auto for_each = [&handles](auto action) {
    for (const tabs::TabHandle& handle : handles) {
      if (tabs::TabInterface* each = handle.Get()) {
        action(each);
      }
    }
  };
  switch (command_id) {
    case TabStripModel::CommandTogglePinned: {
      // Pin or unpin them all the way the clicked tab goes, like upstream.
      const bool pin = !IsPinnedForUser(tab);
      for_each([&](tabs::TabInterface* each) {
        if (pin != IsPinnedForUser(each)) {
          controller->TogglePin(each);
        }
      });
      break;
    }
    case kAddToEssentials:
      for_each(
          [&](tabs::TabInterface* each) { controller->AddToEssentials(each); });
      break;
    case kRemoveFromEssentials:
      for_each([&](tabs::TabInterface* each) {
        controller->RemoveFromEssentials(each);
      });
      break;
    case kChangeLabel:
      controller->StartRenaming(tab);
      break;
    case kChangeIcon:
      controller->ShowIconPicker(tab);
      break;
    case kUnload:
      controller->UnloadTabs(tabs);
      break;
    case kReplaceWithCurrentUrl:
      controller->ReplacePinnedUrlWithCurrent(tab);
      break;
    case kEditPinnedUrl:
      controller->EditPinnedUrl(tab);
      break;
    case kResetPinnedTab:
      controller->ResetToPinnedUrl(tab);
      break;
  }
  return true;
}

// --- Tab views and data ---

bool HandleCloseTabCommand(tabs::TabInterface* tab) {
  KoanPinnedTabController* controller = KoanPinnedTabController::ForTab(tab);
  return controller && controller->HandleCloseShortcut(tab);
}

bool HandleTabCloseRequest(const tabs::TabInterface* tab) {
  KoanPinnedTabController* controller = KoanPinnedTabController::ForTab(tab);
  return controller &&
         controller->HandleCloseRequest(const_cast<tabs::TabInterface*>(tab));
}

void AdjustTabData(const tabs::TabInterface* tab, tabs::TabData& data) {
  const KoanTabState* state =
      features::IsPinnedTabsEnabled() ? GetTabState(tab) : nullptr;
  if (!state) {
    return;
  }
  if (!state->custom_title.empty()) {
    data.title = state->custom_title;
  }
  if (!state->custom_icon.empty()) {
    data.favicon = EmojiIcon(state->custom_icon);
    data.should_display_favicon = true;
    data.should_themify_favicon = false;
    data.is_monochrome_favicon = false;
  }
}

// --- Session ---

void OnRestoringPinnedTab(
    content::WebContents* contents,
    const std::string& data,
    bool from_session_restore,
    std::vector<std::unique_ptr<content::NavigationEntry>>& entries,
    int& selected_navigation) {
  if (!features::IsPinnedTabsEnabled()) {
    return;
  }
  KoanTabState state = KoanTabState::Deserialize(data);
  if (state.empty()) {
    return;
  }
  // Back at its pinned URL, with only that entry, after a reset it didn't
  // load for (it was unloaded), and on startup with Zen's
  // zen.pinned-tab-manager.restore-pinned-tabs-to-pinned-url.
  const bool at_pinned_url =
      selected_navigation >= 0 &&
      static_cast<size_t>(selected_navigation) < entries.size() &&
      entries[selected_navigation]->GetURL() == state.pinned_url;
  Profile* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
  const bool restore_to_pinned_url =
      from_session_restore &&
      profile->GetPrefs()->GetBoolean(prefs::kPinnedTabRestoreToPinnedUrl);
  if (state.pinned_url.is_valid() && !at_pinned_url &&
      (state.reset_pending || restore_to_pinned_url)) {
    entries = PinnedUrlEntries(state.pinned_url, profile);
    selected_navigation = 0;
  }
  state.reset_pending = false;
  SetTabSessionData(contents, kPinnedTabSessionKey, state.Serialize());
  GetOrCreateTabState(contents) = std::move(state);
}

}  // namespace koan
