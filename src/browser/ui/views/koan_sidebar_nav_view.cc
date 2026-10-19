// Copyright 2026 The Koan Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_sidebar_nav_view.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "components/url_formatter/url_formatter.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/ui/views/koan_window_controls.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view_class_properties.h"
#include "url/gurl.h"

namespace koan {

namespace {

constexpr int kButtonSize = 28;
constexpr int kPillHeight = 32;
constexpr int kPadding = 12;

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

}  // namespace

KoanSidebarNavView::KoanSidebarNavView(BrowserView* browser_view)
    : browser_view_(browser_view) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(4, kPadding, 8, kPadding), /*between_child_spacing=*/6));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // Row 1: navigation, then window controls at the trailing end.
  auto* row = AddChildView(std::make_unique<views::BoxLayoutView>());
  row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  row->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
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
    return row->AddChildView(std::move(button));
  };
  back_ = nav_button(vector_icons::kBackArrowChromeRefreshOldIcon, IDC_BACK,
                     u"Back");
  forward_ = nav_button(vector_icons::kForwardArrowChromeRefreshOldIcon,
                        IDC_FORWARD, u"Forward");
  nav_button(vector_icons::kReloadChromeRefreshOldIcon, IDC_RELOAD, u"Reload");

  auto* spacer = row->AddChildView(std::make_unique<views::View>());
  row->SetFlexForView(spacer, 1);
  const gfx::Size caption = GetCaptionButtonAreaSize(browser_view_);
  if (!caption.IsEmpty()) {
    // The page card covers the frame's caption buttons in this layout.
    row->AddChildView(CreateWindowControls(
        browser_view_, gfx::Size(kButtonSize, kButtonSize)));
  }

  // Row 2: the URL pill.
  url_pill_ = AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(&KoanSidebarNavView::FocusOmnibox,
                          base::Unretained(this))));
  url_pill_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  url_pill_->SetPreferredSize(gfx::Size(0, kPillHeight));
  url_pill_->SetBackground(views::CreateRoundedRectBackground(
      kColorTabBackgroundActiveFrameActive, kPillHeight / 2));
  url_pill_->SetElideBehavior(gfx::ELIDE_TAIL);
  url_pill_->SetImageLabelSpacing(0);
  url_pill_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 12)));
  url_pill_->SetTooltipText(u"Search or enter address (Ctrl+L)");

  chrome::AddCommandObserver(browser, IDC_BACK, this);
  chrome::AddCommandObserver(browser, IDC_FORWARD, this);
  back_->SetEnabled(chrome::IsCommandEnabled(browser, IDC_BACK));
  forward_->SetEnabled(chrome::IsCommandEnabled(browser, IDC_FORWARD));
  browser->GetTabStripModel()->AddObserver(this);
  Observe(browser->GetTabStripModel()->GetActiveWebContents());
  UpdateUrl();
}

KoanSidebarNavView::~KoanSidebarNavView() {
  BrowserWindowInterface* browser = browser_view_->browser();
  chrome::RemoveCommandObserver(browser, IDC_BACK, this);
  chrome::RemoveCommandObserver(browser, IDC_FORWARD, this);
  browser->GetTabStripModel()->RemoveObserver(this);
}

void KoanSidebarNavView::EnabledStateChangedForCommand(int id, bool enabled) {
  if (id == IDC_BACK) {
    back_->SetEnabled(enabled);
  } else if (id == IDC_FORWARD) {
    forward_->SetEnabled(enabled);
  }
}

void KoanSidebarNavView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    Observe(tab_strip_model->GetActiveWebContents());
    UpdateUrl();
  }
}

void KoanSidebarNavView::PrimaryPageChanged(content::Page& page) {
  UpdateUrl();
}

void KoanSidebarNavView::TitleWasSet(content::NavigationEntry* entry) {
  UpdateUrl();
}

void KoanSidebarNavView::UpdateUrl() {
  const std::u16string text = DisplayUrl(web_contents());
  url_pill_->SetText(text.empty() ? u"Search or enter address" : text);
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
