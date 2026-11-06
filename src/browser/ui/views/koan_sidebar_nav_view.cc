// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_sidebar_nav_view.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_specification.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "chrome/browser/ui/views/permissions/chip/permission_chip_view.h"
#include "chrome/browser/ui/views/permissions/chip/permission_dashboard_view.h"
#include "chrome/common/webui_url_constants.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "components/url_formatter/url_formatter.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/browser/ui/views/koan_sidebar_header.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_url_bar_controller.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/proposed_layout.h"
#include "url/gurl.h"

namespace koan {

namespace {

constexpr int kButtonSize = 28;
// Gap between the navigation buttons and what's before them in the row.
constexpr int kTrailingGap = 4;

std::u16string DisplayUrl(content::WebContents* contents, Profile* profile) {
  if (!contents) {
    return std::u16string();
  }
  const GURL& url = contents->GetVisibleURL();
  // An empty tab, or a new tab page (also one served from the search engine,
  // e.g. google.com's), shows Zen's placeholder.
  if (url.is_empty() || url.IsAboutBlank() ||
      url == GURL(chrome::kChromeUINewTabURL) || search::IsNTPURL(url) ||
      search::IsNTPOrRelatedURL(url, profile)) {
    return std::u16string();
  }
  if (url.SchemeIsHTTPOrHTTPS() && url.has_host()) {
    // Zen shows just the site in the sidebar.
    return base::UTF8ToUTF16(url_formatter::StripWWW(std::string(url.host())));
  }
  return url_formatter::FormatUrl(url);
}

bool IsTrailingView(const views::View* view) {
  return view->GetClassName() == "KoanSidebarNavButtons";
}

}  // namespace

void LayoutSidebarHeaderTrailingViews(const views::View* host,
                                      views::ProposedLayout& layout,
                                      int right,
                                      int center_y) {
  // Whatever is already laid out on this row, to the left, limits how far
  // left we can go.
  int min_left = 0;
  for (const views::ChildLayout& child : layout.child_layouts) {
    if (child.visible && child.bounds.right() <= right &&
        child.bounds.y() <= center_y && child.bounds.bottom() >= center_y) {
      min_left = std::max(min_left, child.bounds.right() + kTrailingGap);
    }
  }
  for (views::View* child : host->children()) {
    if (!IsTrailingView(child)) {
      continue;
    }
    const gfx::Size size = child->GetPreferredSize();
    const gfx::Rect bounds(right - size.width(), center_y - size.height() / 2,
                           size.width(), size.height());
    const bool fits = bounds.x() >= min_left;
    layout.child_layouts.emplace_back(child, fits, bounds);
    if (fits) {
      right = bounds.x() - kTrailingGap;
    }
  }
}

bool ShowsExpandedSidebarHeader(const views::View* host) {
  const views::Widget* widget = host->GetWidget();
  const BrowserView* browser_view =
      widget ? views::AsViewClass<BrowserView>(widget->client_view()) : nullptr;
  return browser_view && IsCompactModeActive(browser_view);
}

int GetSidebarHeaderTrailingWidth(const views::View* host) {
  int width = 0;
  for (const views::View* child : host->children()) {
    if (IsTrailingView(child)) {
      width += child->GetPreferredSize().width() + kTrailingGap;
    }
  }
  return width;
}

void MirrorSidebarHeaderLayout(const views::View* host,
                               views::ProposedLayout& layout,
                               int width) {
  if (!IsSidebarOnRight(host)) {
    return;
  }
  for (views::ChildLayout& child : layout.child_layouts) {
    child.bounds.set_x(width - child.bounds.right());
  }
}

KoanSidebarNavButtons::KoanSidebarNavButtons(BrowserView* browser_view)
    : browser_view_(browser_view) {
  SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  BrowserWindowInterface* browser = browser_view_->browser();
  const auto nav_button = [&](const gfx::VectorIcon& icon, int command,
                              std::u16string name) {
    auto button = views::CreateVectorImageButtonWithNativeTheme(
        base::BindRepeating(
            [](BrowserWindowInterface* browser, int command) {
              chrome::ExecuteCommand(browser, command);
            },
            base::Unretained(browser), command),
        icon, /*dip_size=*/18);
    button->SetTooltipText(name);
    button->GetViewAccessibility().SetName(name);
    button->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
    return AddChildView(std::move(button));
  };
  back_ = nav_button(vector_icons::kBackArrowChromeRefreshOldIcon, IDC_BACK,
                     u"Back");
  forward_ = nav_button(vector_icons::kForwardArrowChromeRefreshOldIcon,
                        IDC_FORWARD, u"Forward");
  nav_button(vector_icons::kReloadChromeRefreshOldIcon, IDC_RELOAD, u"Reload");

  chrome::AddCommandObserver(browser, IDC_BACK, this);
  chrome::AddCommandObserver(browser, IDC_FORWARD, this);
  back_->SetEnabled(chrome::IsCommandEnabled(browser, IDC_BACK));
  forward_->SetEnabled(chrome::IsCommandEnabled(browser, IDC_FORWARD));
}

KoanSidebarNavButtons::~KoanSidebarNavButtons() {
  BrowserWindowInterface* browser = browser_view_->browser();
  chrome::RemoveCommandObserver(browser, IDC_BACK, this);
  chrome::RemoveCommandObserver(browser, IDC_FORWARD, this);
}

void KoanSidebarNavButtons::EnabledStateChangedForCommand(int id,
                                                          bool enabled) {
  if (id == IDC_BACK) {
    back_->SetEnabled(enabled);
  } else if (id == IDC_FORWARD) {
    forward_->SetEnabled(enabled);
  }
}

BEGIN_METADATA(KoanSidebarNavButtons)
END_METADATA

// The URL pill, a button: pressing it (anywhere but its buttons) edits the
// address; hovering it (or its buttons) lightens it (Zen's #urlbar:hover
// .urlbar-background) and shows the copy button.
class KoanUrlPill : public views::Button {
  METADATA_HEADER(KoanUrlPill, views::Button)

 public:
  KoanUrlPill(PressedCallback on_press,
              base::RepeatingCallback<void(bool)> on_hover)
      : views::Button(std::move(on_press)), on_hover_(std::move(on_hover)) {
    SetNotifyEnterExitOnChild(true);
    UpdateBackground();
  }

  bool GetHovered() const { return hovered_; }

  // views::Button:
  void OnMouseEntered(const ui::MouseEvent& event) override {
    views::Button::OnMouseEntered(event);
    SetHovered(true);
  }
  void OnMouseExited(const ui::MouseEvent& event) override {
    views::Button::OnMouseExited(event);
    SetHovered(false);
  }

 private:
  void SetHovered(bool hovered) {
    if (hovered_ == hovered) {
      return;
    }
    hovered_ = hovered;
    UpdateBackground();
    on_hover_.Run(hovered);
  }

  // A fill only, no border or shadow (Zen's .urlbar-background).
  void UpdateBackground() {
    SetBackground(views::CreateRoundedRectBackground(
        hovered_ ? kColorKoanUrlPillHovered : kColorKoanUrlPill,
        UrlPillCornerRadius()));
  }

  base::RepeatingCallback<void(bool)> on_hover_;
  bool hovered_ = false;
};

BEGIN_METADATA(KoanUrlPill)
ADD_READONLY_PROPERTY_METADATA(bool, Hovered)
END_METADATA

KoanSidebarNavView::KoanSidebarNavView(BrowserView* browser_view)
    : browser_view_(browser_view) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(2, kUrlPillMargin, 8, kUrlPillMargin)));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // Zen's single-toolbar URL bar: the site identity button (the favicon),
  // the site's name, then the copy-URL button (on hover). Chromium's page
  // actions (site settings, install, bookmark star) are not in it.
  auto pill = std::make_unique<KoanUrlPill>(
      base::BindRepeating(&KoanSidebarNavView::FocusOmnibox,
                          base::Unretained(this)),
      base::BindRepeating(&KoanSidebarNavView::OnPillHovered,
                          base::Unretained(this)));
  auto* pill_layout = pill->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(0, kUrlPillLeadingInset, 0, kUrlPillTrailingInset),
      kUrlPillSpacing));
  pill_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  pill->SetPreferredSize(gfx::Size(0, kUrlPillHeight));
  // The focus ring follows the pill's corners.
  views::InstallRoundRectHighlightPathGenerator(pill.get(), gfx::Insets(),
                                                UrlPillCornerRadius());
#if BUILDFLAG(IS_MAC)
  pill->SetTooltipText(u"Search or enter address (\u2318L)");
#else
  pill->SetTooltipText(u"Search or enter address (Ctrl+L)");
#endif
  pill->GetViewAccessibility().SetDescription(u"Search or enter address");
  url_pill_ = AddChildView(std::move(pill));

  const auto pill_button = [&](views::Button::PressedCallback callback,
                               const gfx::VectorIcon& icon,
                               std::u16string name) {
    auto button = views::CreateVectorImageButtonWithNativeTheme(
        std::move(callback), icon, kUrlPillIconSize);
    button->SetTooltipText(name);
    button->GetViewAccessibility().SetName(name);
    button->SetPreferredSize(gfx::Size(kUrlPillButtonSize, kUrlPillButtonSize));
    return url_pill_->AddChildView(std::move(button));
  };
  // Zen's identity box at the start of the pill: the site's favicon, which
  // opens Chromium's page info for the site (security, permissions, cookies,
  // site settings), as Zen's site data panel does; a magnifier on an empty
  // tab. The image is set in UpdateUrl().
  site_button_ =
      pill_button(base::BindRepeating(&KoanSidebarNavView::OnSiteButtonPressed,
                                      base::Unretained(this)),
                  vector_icons::kSearchIcon, u"Site information");

  // Something is in use (camera, microphone ...): see UpdateInUse().
  in_use_ = url_pill_->AddChildView(std::make_unique<views::ImageView>());
  in_use_->SetVisible(false);

  url_label_ = url_pill_->AddChildView(std::make_unique<views::Label>());
  url_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  url_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  url_label_->SetFontList(
      url_label_->font_list()
          .DeriveWithSizeDelta(kUrlPillFontSize -
                               url_label_->font_list().GetFontSize())
          .DeriveWithWeight(gfx::Font::Weight::MEDIUM));
  url_label_->SetEnabledColor(kColorKoanUrlPillForeground);
  url_label_->SetAutoColorReadabilityEnabled(false);
  // The pill's tint is translucent and the sidebar's layer is not opaque:
  // no subpixel text (Label::PaintText() DCHECKs an opaque background).
  url_label_->SetSubpixelRenderingEnabled(false);
  url_label_->SetCanProcessEventsWithinSubtree(false);
  // The pill is named after the site already.
  url_label_->GetViewAccessibility().SetIsIgnored(true);
  pill_layout->SetFlexForView(url_label_, 1);
  // Zen's #zen-copy-url-button ("Copy URL"), confirmed with Zen's toast.
  copy_button_ =
      pill_button(base::BindRepeating(&CopyCurrentUrl,
                                      base::Unretained(browser_view_.get()),
                                      /*markdown=*/false),
                  vector_icons::kContentCopyIcon, u"Copy URL");
  // Like Zen's (opacity 0 until hovered), it keeps its place, so Tab and
  // screen readers reach it.
  copy_button_->SetPaintToLayer();
  copy_button_->layer()->SetFillsBoundsOpaquely(false);
  copy_button_observation_.Observe(copy_button_.get());
  if (LocationBarView* location_bar = browser_view_->GetLocationBarView()) {
    if (PermissionDashboardView* dashboard =
            location_bar->permission_dashboard_view()) {
      chips_observation_.AddObservation(dashboard->GetIndicatorChip());
      chips_observation_.AddObservation(dashboard->GetRequestChip());
    }
  }
  UpdateInUse();

  browser_view_->browser()->GetTabStripModel()->AddObserver(this);
  ObserveActiveTab();
}

KoanSidebarNavView::~KoanSidebarNavView() {
  browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
}

void KoanSidebarNavView::OnPillHovered(bool hovered) {
  hovered_ = hovered;
  UpdateCopyButtonOpacity();
}

void KoanSidebarNavView::OnViewVisibilityChanged(views::View* observed_view,
                                                 views::View* starting_view,
                                                 bool visible) {
  UpdateInUse();
}

void KoanSidebarNavView::OnViewIsDeleting(views::View* observed_view) {
  if (chips_observation_.IsObservingSource(observed_view)) {
    chips_observation_.RemoveObservation(observed_view);
  }
  if (copy_button_observation_.IsObservingSource(observed_view)) {
    copy_button_observation_.Reset();
  }
}

void KoanSidebarNavView::UpdateInUse() {
  LocationBarView* location_bar = browser_view_->GetLocationBarView();
  PermissionDashboardView* dashboard =
      location_bar ? location_bar->permission_dashboard_view() : nullptr;
  // The indicator (in use) before a request.
  PermissionChipView* chip = nullptr;
  bool in_use = false;
  if (dashboard && dashboard->GetVisible()) {
    if (chips_observation_.IsObservingSource(dashboard->GetIndicatorChip()) &&
        dashboard->GetIndicatorChip()->GetVisible()) {
      chip = dashboard->GetIndicatorChip();
      in_use = true;
    } else if (chips_observation_.IsObservingSource(
                   dashboard->GetRequestChip()) &&
               dashboard->GetRequestChip()->GetVisible()) {
      chip = dashboard->GetRequestChip();
    }
  }
  const gfx::ImageSkia icon =
      chip ? chip->GetImage(views::Button::STATE_NORMAL) : gfx::ImageSkia();
  in_use_->SetVisible(chip && !icon.isNull());
  if (!in_use_->GetVisible()) {
    return;
  }
  // The chip's own icon (drawn for the chip's colours), in the colour of the
  // pill, red while in use.
  const ui::ColorId color =
      in_use ? static_cast<ui::ColorId>(ui::kColorAlertHighSeverity)
             : static_cast<ui::ColorId>(kColorKoanUrlPillForeground);
  in_use_->SetImage(ui::ImageModel::FromImageGenerator(
      base::BindRepeating(
          [](gfx::ImageSkia icon, ui::ColorId color,
             const ui::ColorProvider* provider) {
            return gfx::ImageSkiaOperations::CreateColorMask(
                icon, provider ? provider->GetColor(color) : SK_ColorGRAY);
          },
          icon, color),
      icon.size()));
  std::u16string tooltip = chip->GetTooltipText();
  if (tooltip.empty()) {
    tooltip = in_use ? u"Camera, microphone or location in use"
                     : u"A site wants a permission";
  }
  in_use_->SetTooltipText(tooltip);
  in_use_->GetViewAccessibility().SetName(tooltip);
}

void KoanSidebarNavView::OnViewFocused(views::View* observed_view) {
  UpdateCopyButtonOpacity();
}

void KoanSidebarNavView::OnViewBlurred(views::View* observed_view) {
  UpdateCopyButtonOpacity();
}

void KoanSidebarNavView::UpdateCopyButtonOpacity() {
  copy_button_->layer()->SetOpacity(
      hovered_ || copy_button_->HasFocus() ? 1.0f : 0.0f);
}

views::View* KoanSidebarNavView::bubble_anchor() const {
  for (views::View* view :
       {static_cast<views::View*>(site_button_.get()), url_pill_.get()}) {
    if (view && view->IsDrawn() && !view->GetVisibleBounds().IsEmpty()) {
      return view;
    }
  }
  return nullptr;
}

void KoanSidebarNavView::SetEditing(bool editing) {
  if (editing_ == editing) {
    return;
  }
  editing_ = editing;
  UpdateUrl();
}

void KoanSidebarNavView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // A replaced tab (e.g. discarded and reloaded) can be the active one
  // without a selection change.
  if (selection.active_tab_changed() ||
      change.type() == TabStripModelChange::kReplaced) {
    ObserveActiveTab();
  }
}

void KoanSidebarNavView::OnTabChangedAt(tabs::TabInterface* tab,
                                        TabChangeType change_type) {
  // The favicon arrives after the navigation (and with it the TabUIHelper
  // callback): refresh the identity button when the active tab changes.
  if (tab &&
      tab == browser_view_->browser()->GetTabStripModel()->GetActiveTab()) {
    UpdateUrl();
  }
}

void KoanSidebarNavView::ObserveActiveTab() {
  tab_ui_subscription_ = {};
  tabs::TabInterface* tab =
      browser_view_->browser()->GetTabStripModel()->GetActiveTab();
  if (TabUIHelper* helper = tab ? TabUIHelper::From(tab) : nullptr) {
    tab_ui_subscription_ = helper->AddTabUIChangeCallback(base::BindRepeating(
        &KoanSidebarNavView::UpdateUrl, base::Unretained(this)));
  }
  UpdateUrl();
}

void KoanSidebarNavView::UpdateUrl() {
  content::WebContents* contents =
      browser_view_->browser()->GetTabStripModel()->GetActiveWebContents();
  const std::u16string text = DisplayUrl(contents, browser_view_->GetProfile());
  // Zen's placeholder for an empty tab.
  url_label_->SetText(editing_       ? std::u16string()
                      : text.empty() ? u"Search..."
                                     : text);
  // The pill reads as the site it shows.
  url_pill_->GetViewAccessibility().SetName(
      text.empty() ? u"Search or enter address" : text);
  // Like Zen, the copy button is only there for a page (not an empty tab).
  const bool has_page = !editing_ && !text.empty();
  has_page_ = !text.empty();
  copy_button_->SetVisible(
      has_page &&
      browser_view_->GetProfile()->GetOriginalProfile()->GetPrefs()->GetBoolean(
          prefs::kUrlBarShowCopyUrl));
  UpdateCopyButtonOpacity();
  // The identity button: the page's favicon, a magnifier on an empty tab, and
  // nothing while the URL bar edits over the pill (it has its own icon).
  site_button_->SetVisible(!editing_);
  ui::ImageModel favicon;
  if (has_page) {
    tabs::TabInterface* tab =
        browser_view_->browser()->GetTabStripModel()->GetActiveTab();
    if (TabUIHelper* helper = tab ? TabUIHelper::From(tab) : nullptr) {
      favicon = helper->GetFavicon();
    }
  }
  site_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      !favicon.IsEmpty() ? favicon
                         : ui::ImageModel::FromVectorIcon(
                               vector_icons::kSearchIcon,
                               kColorKoanUrlPillForeground, kUrlPillIconSize));
  const std::u16string site_name =
      has_page ? u"Site information" : u"Search or enter address";
  site_button_->SetTooltipText(site_name);
  site_button_->GetViewAccessibility().SetName(site_name);
}

void KoanSidebarNavView::OnSiteButtonPressed() {
  // On an empty tab the magnifier edits the address, like the pill.
  if (!has_page_) {
    FocusOmnibox();
    return;
  }
  ShowSitePanel();
}

void KoanSidebarNavView::FocusOmnibox() {
  if (LocationBarView* location_bar = browser_view_->GetLocationBarView()) {
    location_bar->FocusLocation(/*is_user_initiated=*/true,
                                /*clear_focus_if_failed=*/false);
  }
}

void KoanSidebarNavView::ShowSitePanel() {
  content::WebContents* contents =
      browser_view_->browser()->GetTabStripModel()->GetActiveWebContents();
  content::NavigationEntry* entry =
      contents ? contents->GetController().GetVisibleEntry() : nullptr;
  if (!entry || entry->IsInitialEntry() || !GetWidget()) {
    return;
  }
  // The pill is in the peeked sidebar (compact mode): keep it out while the
  // bubble that points at it is open.
  if (KoanBrowserViewController* controller =
          KoanBrowserViewController::From(browser_view_)) {
    controller->compact_mode()->HoldSidebarForBubble();
  }
  PageInfoBubbleView::CreatePageInfoBubble(
      PageInfoBubbleSpecification::Builder(
          views::BubbleAnchor(site_button_.get()),
          GetWidget()->GetNativeWindow(), contents, entry->GetVirtualURL())
          .Build())
      ->GetWidget()
      ->Show();
}

BEGIN_METADATA(KoanSidebarNavView)
END_METADATA

}  // namespace koan
