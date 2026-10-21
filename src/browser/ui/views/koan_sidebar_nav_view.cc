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
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_specification.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
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
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_sidebar_header.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_url_bar_controller.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/proposed_layout.h"
#include "url/gurl.h"

namespace koan {

namespace {

constexpr int kButtonSize = 28;
constexpr int kPillHeight = 36;
constexpr int kPadding = 12;
// Gap between the navigation buttons and what's before them in the row.
constexpr int kTrailingGap = 4;

std::u16string DisplayUrl(content::WebContents* contents) {
  if (!contents) {
    return std::u16string();
  }
  const GURL& url = contents->GetVisibleURL();
  // An empty tab shows Zen's placeholder.
  if (url.is_empty() || url.IsAboutBlank() ||
      url == GURL(chrome::kChromeUINewTabURL)) {
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
// address; hovering it (or its buttons) shows the copy button.
class KoanUrlPill : public views::Button {
  METADATA_HEADER(KoanUrlPill, views::Button)

 public:
  KoanUrlPill(PressedCallback on_press,
              base::RepeatingCallback<void(bool)> on_hover)
      : views::Button(std::move(on_press)), on_hover_(std::move(on_hover)) {
    SetNotifyEnterExitOnChild(true);
  }

  // views::Button:
  void OnMouseEntered(const ui::MouseEvent& event) override {
    views::Button::OnMouseEntered(event);
    on_hover_.Run(true);
  }
  void OnMouseExited(const ui::MouseEvent& event) override {
    views::Button::OnMouseExited(event);
    on_hover_.Run(false);
  }

 private:
  base::RepeatingCallback<void(bool)> on_hover_;
};

BEGIN_METADATA(KoanUrlPill)
END_METADATA

KoanSidebarNavView::KoanSidebarNavView(BrowserView* browser_view)
    : browser_view_(browser_view) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(2, kPadding, 8, kPadding)));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // Zen's single-toolbar URL bar: the site's name, then the copy-URL button
  // (on hover) and the site panel button at the end.
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
  pill->SetPreferredSize(gfx::Size(0, kPillHeight));
  pill->SetBackground(views::CreateRoundedRectBackground(kColorKoanUrlPill,
                                                         kUrlPillCornerRadius));
  // The focus ring follows the pill's corners.
  views::InstallRoundRectHighlightPathGenerator(pill.get(), gfx::Insets(),
                                                kUrlPillCornerRadius);
#if BUILDFLAG(IS_MAC)
  pill->SetTooltipText(u"Search or enter address (\u2318L)");
#else
  pill->SetTooltipText(u"Search or enter address (Ctrl+L)");
#endif
  pill->GetViewAccessibility().SetDescription(u"Search or enter address");
  url_pill_ = AddChildView(std::move(pill));

  url_label_ = url_pill_->AddChildView(std::make_unique<views::Label>());
  url_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  url_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  url_label_->SetFontList(
      url_label_->font_list().DeriveWithWeight(gfx::Font::Weight::MEDIUM));
  url_label_->SetCanProcessEventsWithinSubtree(false);
  pill_layout->SetFlexForView(url_label_, 1);

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
  // Zen's #zen-copy-url-button ("Copy URL"), confirmed with Zen's toast.
  copy_button_ =
      pill_button(base::BindRepeating(&CopyCurrentUrl,
                                      base::Unretained(browser_view_.get()),
                                      /*markdown=*/false),
                  vector_icons::kContentCopyIcon, u"Copy URL");
  copy_button_->SetVisible(false);
  // Zen's site panel button (#zen-site-data-icon-button): Chromium's page
  // info for the site (security, permissions, cookies, site settings).
  site_button_ =
      pill_button(base::BindRepeating(&KoanSidebarNavView::ShowSitePanel,
                                      base::Unretained(this)),
                  vector_icons::kTuneIcon, u"Site information");

  browser_view_->browser()->GetTabStripModel()->AddObserver(this);
  ObserveActiveTab();
}

KoanSidebarNavView::~KoanSidebarNavView() {
  browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
}

void KoanSidebarNavView::OnPillHovered(bool hovered) {
  hovered_ = hovered;
  UpdateUrl();
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
  const std::u16string text = DisplayUrl(contents);
  // Zen's placeholder for an empty tab.
  url_label_->SetText(editing_       ? std::u16string()
                      : text.empty() ? u"Search..."
                                     : text);
  // The pill reads as the site it shows (the label isn't exposed itself).
  url_pill_->GetViewAccessibility().SetName(
      text.empty() ? u"Search or enter address" : text);
  // Like Zen, the buttons are only there for a page (not an empty tab).
  const bool has_page = !editing_ && !text.empty();
  copy_button_->SetVisible(
      has_page && hovered_ &&
      browser_view_->GetProfile()->GetOriginalProfile()->GetPrefs()->GetBoolean(
          prefs::kUrlBarShowCopyUrl));
  site_button_->SetVisible(has_page);
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
