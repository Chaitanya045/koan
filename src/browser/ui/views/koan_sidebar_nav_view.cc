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
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "components/tabs/public/tab_interface.h"
#include "components/url_formatter/url_formatter.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/ui/views/koan_sidebar_header.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/proposed_layout.h"
#include "ui/views/view_class_properties.h"
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

KoanSidebarNavView::KoanSidebarNavView(BrowserView* browser_view)
    : browser_view_(browser_view) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(2, kPadding, 8, kPadding)));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  url_pill_ =
      AddChildView(std::make_unique<views::LabelButton>(base::BindRepeating(
          &KoanSidebarNavView::FocusOmnibox, base::Unretained(this))));
  url_pill_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  url_pill_->SetPreferredSize(gfx::Size(0, kPillHeight));
  url_pill_->SetBackground(views::CreateRoundedRectBackground(
      kColorTabBackgroundActiveFrameActive, kPillHeight / 4));
  url_pill_->SetElideBehavior(gfx::ELIDE_TAIL);
  url_pill_->SetImageLabelSpacing(10);
  url_pill_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 10)));
  url_pill_->SetTooltipText(u"Search or enter address (Ctrl+L)");

  browser_view_->browser()->GetTabStripModel()->AddObserver(this);
  ObserveActiveTab();
}

KoanSidebarNavView::~KoanSidebarNavView() {
  browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
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
  if (editing_) {
    url_pill_->SetText(std::u16string());
    url_pill_->SetImageModel(views::Button::STATE_NORMAL, ui::ImageModel());
    return;
  }
  tabs::TabInterface* tab =
      browser_view_->browser()->GetTabStripModel()->GetActiveTab();
  const std::u16string text = DisplayUrl(tab ? tab->GetContents() : nullptr);
  url_pill_->SetText(text.empty() ? u"Search or enter address" : text);

  // The site's icon, as the tab shows it, or a globe.
  ui::ImageModel icon;
  if (TabUIHelper* helper = tab ? TabUIHelper::From(tab) : nullptr;
      helper && helper->ShouldDisplayFavicon()) {
    icon = helper->GetFavicon();
  }
  if (icon.IsEmpty()) {
    icon = ui::ImageModel::FromVectorIcon(vector_icons::kGlobeIcon,
                                          kColorTabForegroundActiveFrameActive,
                                          gfx::kFaviconSize);
  }
  url_pill_->SetImageModel(views::Button::STATE_NORMAL, icon);
}

void KoanSidebarNavView::FocusOmnibox() {
  if (LocationBarView* location_bar = browser_view_->GetLocationBarView()) {
    location_bar->FocusLocation(/*is_user_initiated=*/true,
                                /*clear_focus_if_failed=*/false);
  }
}

BEGIN_METADATA(KoanSidebarNavView)
END_METADATA

}  // namespace koan
