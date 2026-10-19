// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_workspaces.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/i18n/break_iterator.h"
#include "base/i18n/char_iterator.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_group_theme.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "components/prefs/pref_service.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "third_party/icu/source/common/unicode/uchar.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/themes.mojom.h"
#include "ui/color/color_provider.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace koan {

namespace {

constexpr int kButtonSize = 24;
constexpr int kButtonSpacing = 6;

// Colours offered to new workspaces, in order.
// Colours offered to new workspaces, in order, with a default icon each
// (Zen workspaces have an emoji icon; rename a workspace to change it).
constexpr struct {
  tab_groups::TabGroupColorId color;
  const char16_t* icon;
} kWorkspaceStyles[] = {
    {tab_groups::TabGroupColorId::kBlue, u"\U0001F30A"},    // water wave
    {tab_groups::TabGroupColorId::kGreen, u"\U0001F33F"},   // herb
    {tab_groups::TabGroupColorId::kPurple, u"\U0001F52E"},  // crystal ball
    {tab_groups::TabGroupColorId::kOrange, u"\U0001F34A"},  // tangerine
    {tab_groups::TabGroupColorId::kPink, u"\U0001F338"},    // cherry blossom
    {tab_groups::TabGroupColorId::kCyan, u"\U0001F4A7"},    // droplet
    {tab_groups::TabGroupColorId::kRed, u"\U0001F525"},     // fire
    {tab_groups::TabGroupColorId::kYellow, u"\u2B50"},      // star
};

// Splits a workspace name into its icon and display name. A leading emoji
// (one grapheme cluster, so ZWJ sequences and flags stay whole) is the icon;
// otherwise the icon is the name's first character.
std::pair<std::u16string, std::u16string> SplitIcon(std::u16string name) {
  base::i18n::BreakIterator iter(name,
                                 base::i18n::BreakIterator::BREAK_CHARACTER);
  if (name.empty() || !iter.Init() || !iter.Advance()) {
    return {std::u16string(), std::move(name)};
  }
  std::u16string first(iter.GetString());
  const int32_t code_point = base::i18n::UTF16CharIterator(first).get();
  const bool emoji =
      u_hasBinaryProperty(code_point, UCHAR_EXTENDED_PICTOGRAPHIC) ||
      u_hasBinaryProperty(code_point, UCHAR_REGIONAL_INDICATOR);
  if (!emoji) {
    return {std::move(first), std::move(name)};
  }
  std::u16string rest;
  base::TrimWhitespace(name.substr(first.size()), base::TRIM_LEADING, &rest);
  return {std::move(first), rest.empty() ? first : std::move(rest)};
}

// Seed colours for workspace tints, one per tab group colour. Fixed values
// (not looked up from the current theme, which the tint itself changes).
SkColor TintSeed(tab_groups::TabGroupColorId color) {
  switch (color) {
    case tab_groups::TabGroupColorId::kGrey:
      return SkColorSetRGB(0x5F, 0x63, 0x68);
    case tab_groups::TabGroupColorId::kBlue:
      return SkColorSetRGB(0x1A, 0x73, 0xE8);
    case tab_groups::TabGroupColorId::kRed:
      return SkColorSetRGB(0xD9, 0x30, 0x25);
    case tab_groups::TabGroupColorId::kYellow:
      return SkColorSetRGB(0xF9, 0xAB, 0x00);
    case tab_groups::TabGroupColorId::kGreen:
      return SkColorSetRGB(0x1E, 0x8E, 0x3E);
    case tab_groups::TabGroupColorId::kPink:
      return SkColorSetRGB(0xD0, 0x18, 0x84);
    case tab_groups::TabGroupColorId::kPurple:
      return SkColorSetRGB(0x93, 0x34, 0xE6);
    case tab_groups::TabGroupColorId::kCyan:
      return SkColorSetRGB(0x12, 0xB5, 0xCB);
    case tab_groups::TabGroupColorId::kOrange:
      return SkColorSetRGB(0xFA, 0x90, 0x3E);
    case tab_groups::TabGroupColorId::kNumEntries:
      break;
  }
  return SkColorSetRGB(0x1A, 0x73, 0xE8);
}

}  // namespace

// A round workspace button: a filled circle in the workspace's colour with
// its initial, ringed when active.
class KoanWorkspaceButton : public views::Button {
  METADATA_HEADER(KoanWorkspaceButton, views::Button)

 public:
  KoanWorkspaceButton(PressedCallback callback,
                      std::u16string label,
                      std::optional<tab_groups::TabGroupColorId> color,
                      bool active,
                      std::u16string tooltip)
      : views::Button(std::move(callback)),
        label_(std::move(label)),
        emoji_(!label_.empty() && label_.front() > 0x2000),
        color_(color),
        active_(active) {
    SetTooltipText(tooltip);
    GetViewAccessibility().SetName(tooltip);
  }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kButtonSize, kButtonSize);
  }

  const std::u16string& GetLabel() const { return label_; }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* provider = GetColorProvider();
    const bool frame_active = GetWidget() && GetWidget()->ShouldPaintAsActive();
    const SkColor fill =
        color_ ? provider->GetColor(
                     GetTabGroupTabStripColorId(*color_, frame_active))
               : provider->GetColor(kColorTabBackgroundActiveFrameActive);
    const gfx::RectF bounds(GetLocalBounds());
    const gfx::PointF center = bounds.CenterPoint();
    const float radius = std::min(bounds.width(), bounds.height()) / 2.f;

    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    // Inactive workspaces are muted so the active one stands out.
    flags.setColor(active_ || !color_ || GetState() == STATE_HOVERED
                       ? fill
                       : SkColorSetA(fill, 0x80));
    canvas->DrawCircle(center, radius - (active_ ? 3.f : 1.f), flags);

    if (active_) {
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(1.5f);
      flags.setColor(fill);
      canvas->DrawCircle(center, radius - 0.75f, flags);
    }

    const SkColor text = color_utils::GetColorWithMaxContrast(fill);
    canvas->DrawStringRectWithFlags(
        label_, gfx::FontList().DeriveWithSizeDelta(emoji_ ? 1 : -1), text,
        GetLocalBounds(), gfx::Canvas::TEXT_ALIGN_CENTER);
  }

 private:
  const std::u16string label_;
  const bool emoji_;
  const std::optional<tab_groups::TabGroupColorId> color_;
  const bool active_;
};

BEGIN_METADATA(KoanWorkspaceButton)
ADD_READONLY_PROPERTY_METADATA(std::u16string, Label)
END_METADATA

// The row of workspace buttons at the bottom of the sidebar.
class KoanWorkspaceSwitcherView : public views::BoxLayoutView {
  METADATA_HEADER(KoanWorkspaceSwitcherView, views::BoxLayoutView)

 public:
  explicit KoanWorkspaceSwitcherView(KoanWorkspaceController* controller)
      : controller_(controller) {
    SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    SetMainAxisAlignment(views::BoxLayout::MainAxisAlignment::kCenter);
    SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    SetBetweenChildSpacing(kButtonSpacing);
    SetInsideBorderInsets(gfx::Insets::VH(8, 8));
    SetProperty(views::kElementIdentifierKey, kWorkspaceSwitcherElementId);
  }

  DECLARE_CLASS_ELEMENT_IDENTIFIER_VALUE(kWorkspaceSwitcherElementId);

  void Rebuild() {
    RemoveAllChildViews();
    const std::optional<tab_groups::TabGroupId> active =
        controller_->GetActiveWorkspace();
    TabStripModel* model = controller_->tab_strip_model();

    AddChildView(std::make_unique<KoanWorkspaceButton>(
        base::BindRepeating(&KoanWorkspaceController::ShowAllTabs,
                            base::Unretained(controller_)),
        u"∞", std::nullopt, !active.has_value(), u"All tabs"));

    int number = 1;
    for (const tab_groups::TabGroupId& id : controller_->GetWorkspaces()) {
      const TabGroup* group = model->group_model()->GetTabGroup(id);
      const tab_groups::TabGroupVisualData* visuals = group->visual_data();
      auto [label, title] = SplitIcon(visuals->title());
      if (title.empty()) {
        label = base::NumberToString16(number);
        title = u"Workspace " + base::NumberToString16(number);
      }
      AddChildView(std::make_unique<KoanWorkspaceButton>(
          base::BindRepeating(&KoanWorkspaceController::SwitchTo,
                              base::Unretained(controller_), id),
          std::move(label), visuals->color(), active == id, std::move(title)));
      ++number;
    }

    AddChildView(std::make_unique<KoanWorkspaceButton>(
        base::BindRepeating(&KoanWorkspaceController::CreateWorkspace,
                            base::Unretained(controller_)),
        u"+", std::nullopt, /*active=*/false, u"New workspace (Ctrl+Alt+N)"));
  }

 private:
  const raw_ptr<KoanWorkspaceController> controller_;
};

DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(KoanWorkspaceSwitcherView,
                                      kWorkspaceSwitcherElementId);

BEGIN_METADATA(KoanWorkspaceSwitcherView)
END_METADATA

KoanWorkspaceController::KoanWorkspaceController(BrowserView* browser_view)
    : browser_view_(browser_view) {
  if (IsEnabled()) {
    tab_strip_model()->AddObserver(this);
  }
}

KoanWorkspaceController::~KoanWorkspaceController() = default;

bool KoanWorkspaceController::IsEnabled() const {
  return features::IsWorkspacesEnabled() &&
         base::FeatureList::IsEnabled(::features::kTabGroupsFocusing) &&
         browser_view_->GetIsNormalType() &&
         browser_view_->browser()->GetTabStripModel()->SupportsTabGroups();
}

TabStripModel* KoanWorkspaceController::tab_strip_model() const {
  return browser_view_->browser()->GetTabStripModel();
}

void KoanWorkspaceController::OnAddedToWidget() {
  if (!IsEnabled()) {
    return;
  }
  InstallSwitcher();
}

void KoanWorkspaceController::OnBrowserViewDestroying() {
  if (switcher_) {
    switcher_->parent()->RemoveChildViewT(std::exchange(switcher_, nullptr));
  }
  // Unconditional: removing an observer that was never added is a no-op.
  tab_strip_model()->RemoveObserver(this);
}

void KoanWorkspaceController::InstallSwitcher() {
  if (switcher_) {
    return;
  }
  // The switcher goes at the bottom of the vertical tab strip's content area
  // (a FlexLayoutView whose new-tab container flexes to fill the space, so
  // anything appended after it sits at the bottom of the sidebar).
  views::View* region = browser_view_->tab_strip_view();
  if (!region || region->GetClassName() != "VerticalTabStripRegionView") {
    return;
  }
  for (views::View* child : region->children()) {
    if (child->GetClassName() == "FlexLayoutView") {
      switcher_ = child->AddChildView(
          std::make_unique<KoanWorkspaceSwitcherView>(this));
      RebuildSwitcher();
      return;
    }
  }
}

void KoanWorkspaceController::UpdateSwitcher() {
  if (!switcher_ || rebuild_pending_) {
    return;
  }
  rebuild_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanWorkspaceController::RebuildSwitcher,
                                weak_factory_.GetWeakPtr()));
}

void KoanWorkspaceController::RebuildSwitcher() {
  rebuild_pending_ = false;
  if (switcher_) {
    switcher_->Rebuild();
  }
}

std::vector<tab_groups::TabGroupId> KoanWorkspaceController::GetWorkspaces()
    const {
  TabStripModel* model = tab_strip_model();
  if (!model->group_model()) {
    return {};
  }
  std::vector<tab_groups::TabGroupId> groups =
      model->group_model()->ListTabGroups();
  std::ranges::sort(groups, {}, [model](const tab_groups::TabGroupId& id) {
    return model->group_model()->GetTabGroup(id)->ListTabs().start();
  });
  return groups;
}

std::optional<tab_groups::TabGroupId>
KoanWorkspaceController::GetActiveWorkspace() const {
  return tab_strip_model()->GetFocusedGroup();
}

void KoanWorkspaceController::SwitchTo(
    const tab_groups::TabGroupId& workspace) {
  TabStripModel* model = tab_strip_model();
  if (!model->group_model() ||
      !model->group_model()->ContainsTabGroup(workspace) ||
      model->group_model()->GetTabGroup(workspace)->tab_count() == 0) {
    return;
  }
  model->EnterFocusMode(workspace);
}

void KoanWorkspaceController::ShowAllTabs() {
  tab_strip_model()->ExitFocusMode();
}

void KoanWorkspaceController::Cycle(bool forward) {
  tab_strip_model()->RotateFocusedGroup(forward);
}

void KoanWorkspaceController::CreateWorkspace() {
  TabStripModel* model = tab_strip_model();
  const size_t existing = GetWorkspaces().size();
  const auto& style = kWorkspaceStyles[existing % std::size(kWorkspaceStyles)];
  chrome::AddTabAt(browser_view_->browser(), GURL(), model->count(),
                   /*foreground=*/true);
  const tab_groups::TabGroupId group =
      model->AddToNewGroup({model->count() - 1});
  model->ChangeTabGroupVisuals(
      group, tab_groups::TabGroupVisualData(
                 base::StrCat({style.icon, u" Space ",
                               base::NumberToString16(existing + 1)}),
                 style.color));
  model->EnterFocusMode(group);
}

void KoanWorkspaceController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kInserted &&
      tab_strip_model->GetFocusedGroup().has_value() && !adopt_pending_) {
    for (const auto& inserted : change.GetInsert()->contents) {
      if (!inserted.tab->GetGroup() && !inserted.tab->IsPinned()) {
        // New tabs belong to the active workspace. The model can't be
        // modified from inside its own notification, so do it right after.
        adopt_pending_ = true;
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(&KoanWorkspaceController::AdoptUngroupedTabs,
                           weak_factory_.GetWeakPtr()));
        break;
      }
    }
  }
  if (change.type() != TabStripModelChange::kSelectionOnly) {
    UpdateSwitcher();
  }
}

void KoanWorkspaceController::AdoptUngroupedTabs() {
  adopt_pending_ = false;
  TabStripModel* model = tab_strip_model();
  const std::optional<tab_groups::TabGroupId> focused =
      model->GetFocusedGroup();
  if (!focused) {
    return;
  }
  // Adopt the active tab only: Ctrl+T, the new-tab button and Alt+Enter in
  // the omnibox all activate the new tab. Background tabs opened from links
  // already join their opener's group upstream.
  const int active = model->active_index();
  if (active == TabStripModel::kNoTab) {
    return;
  }
  tabs::TabInterface* tab = model->GetTabAtIndex(active);
  if (tab->GetGroup() || tab->IsPinned()) {
    return;
  }
  model->AddToExistingGroup({active}, *focused, /*add_to_end=*/true);
}

void KoanWorkspaceController::OnTabGroupChanged(const TabGroupChange& change) {
  UpdateSwitcher();
  if (change.type == TabGroupChange::kVisualsChanged &&
      change.group == GetActiveWorkspace()) {
    UpdateTint();
  }
}

void KoanWorkspaceController::OnTabGroupFocusChanged(
    std::optional<tab_groups::TabGroupId> new_focused_group_id,
    std::optional<tab_groups::TabGroupId> old_focused_group_id) {
  UpdateSwitcher();
  UpdateTint();
}

void KoanWorkspaceController::UpdateTint() {
  Profile* profile = browser_view_->GetProfile();
  if (profile->IsOffTheRecord()) {
    return;
  }
  PrefService* prefs = profile->GetPrefs();
  ThemeService* theme = ThemeServiceFactory::GetForProfile(profile);
  if (!theme || !prefs->GetBoolean(prefs::kWorkspaceTint) ||
      theme->UsingExtensionTheme()) {
    return;
  }
  const std::optional<tab_groups::TabGroupId> active = GetActiveWorkspace();
  const std::optional<SkColor> current = theme->GetUserColor();
  const SkColor applied =
      static_cast<SkColor>(prefs->GetInteger(prefs::kWorkspaceTintApplied));
  // Whether the browser still shows the tint Koan applied (as opposed to the
  // user's own colour, including one picked while a workspace was active).
  const bool showing_tint = applied && current == applied;
  if (active) {
    if (!showing_tint) {
      prefs->SetInteger(prefs::kWorkspaceTintSavedColor,
                        current ? static_cast<int>(*current) : 0);
      prefs->SetInteger(prefs::kWorkspaceTintSavedVariant,
                        static_cast<int>(theme->GetBrowserColorVariant()));
    }
    const SkColor seed = TintSeed(tab_strip_model()
                                      ->group_model()
                                      ->GetTabGroup(*active)
                                      ->visual_data()
                                      ->color());
    prefs->SetInteger(prefs::kWorkspaceTintApplied, static_cast<int>(seed));
    if (current != seed) {
      theme->SetUserColorAndBrowserColorVariant(
          seed, ui::mojom::BrowserColorVariant::kTonalSpot);
    }
    return;
  }
  // All tabs: put the user's colour back, unless they changed it since.
  prefs->SetInteger(prefs::kWorkspaceTintApplied, 0);
  if (!showing_tint) {
    return;
  }
  const SkColor saved =
      static_cast<SkColor>(prefs->GetInteger(prefs::kWorkspaceTintSavedColor));
  const auto variant = static_cast<ui::mojom::BrowserColorVariant>(
      prefs->GetInteger(prefs::kWorkspaceTintSavedVariant));
  if (saved) {
    theme->SetUserColorAndBrowserColorVariant(saved, variant);
  } else {
    theme->SetUserColor(std::nullopt);
    theme->SetBrowserColorVariant(variant);
  }
}

}  // namespace koan
