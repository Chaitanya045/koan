// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_site_panel_actions.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "build/build_config.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_strip_on_share.h"
#include "koan/browser/ui/views/koan_url_bar_controller.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace koan {

namespace {

// Zen's #zen-site-data-header (zen-single-components.css): 10px 9px 8px
// padding, 8px between buttons, 34px tall (--size-item-large), 6px corners,
// 18px icons. The page info bubble's top margin stands for the top padding.
constexpr int kRowSideInset = 9;
constexpr int kRowBottomInset = 8;
constexpr int kButtonGap = 8;
constexpr int kButtonHeight = 34;
constexpr int kButtonRadius = 6;
constexpr int kIconSize = 18;

// Zen's squircle button: a filled rounded rectangle with the icon, lighter
// while hovered or pressed. Colours are the system's surface colours, which
// Koan's looks map.
class KoanSitePanelButton : public views::Button {
  METADATA_HEADER(KoanSitePanelButton, views::Button)

 public:
  KoanSitePanelButton(PressedCallback callback,
                      const gfx::VectorIcon& icon,
                      std::u16string name,
                      bool active)
      : views::Button(std::move(callback)), icon_(&icon), active_(active) {
    SetTooltipText(name);
    GetViewAccessibility().SetName(name);
    SetPreferredSize(gfx::Size(kButtonHeight, kButtonHeight));
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  kButtonRadius);
  }

  // views::Button:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const ui::ColorProvider* provider = GetColorProvider();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(provider->GetColor(ui::kColorSysNeutralContainer));
    const gfx::RectF bounds(GetLocalBounds());
    canvas->DrawRoundRect(bounds, kButtonRadius, flags);
    if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      flags.setColor(provider->GetColor(GetState() == STATE_PRESSED
                                            ? ui::kColorSysStatePressed
                                            : ui::kColorSysStateHoverOnSubtle));
      canvas->DrawRoundRect(bounds, kButtonRadius, flags);
    }
    // Zen: the active button (a bookmarked page) in the accent colour.
    const ui::ColorId color = !GetEnabled() ? ui::kColorSysStateDisabled
                              : active_     ? ui::kColorSysPrimary
                                            : ui::kColorSysOnSurface;
    canvas->DrawImageInt(
        gfx::CreateVectorIcon(*icon_, kIconSize, provider->GetColor(color)),
        (width() - kIconSize) / 2, (height() - kIconSize) / 2);
  }

 private:
  const raw_ptr<const gfx::VectorIcon> icon_;
  const bool active_;
};

BEGIN_METADATA(KoanSitePanelButton)
END_METADATA

// The row, above page info's pages.
class KoanSitePanelActionsView : public views::BoxLayoutView {
  METADATA_HEADER(KoanSitePanelActionsView, views::BoxLayoutView)

 public:
  KoanSitePanelActionsView(BrowserView* browser_view,
                           content::WebContents* contents)
      : browser_(browser_view->browser()->GetWeakPtr()),
        browser_view_(browser_view),
        contents_(contents->GetWeakPtr()) {
    SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    SetInsideBorderInsets(
        gfx::Insets::TLBR(0, kRowSideInset, kRowBottomInset, kRowSideInset));
    SetBetweenChildSpacing(kButtonGap);

    const GURL url = contents->GetVisibleURL();
    const bool web_page = url.SchemeIsHTTPOrHTTPS();
    bookmarks::BookmarkModel* bookmarks =
        BookmarkModelFactory::GetForBrowserContext(
            contents->GetBrowserContext());
    const bool bookmarked = bookmarks && bookmarks->IsBookmarked(url);

    std::vector<std::unique_ptr<KoanSitePanelButton>> buttons;
    // Zen's order (site-data.inc), Reader Mode in Boost's place.
    buttons.push_back(MakeButton(&KoanSitePanelActionsView::Share,
                                 kKoanShareIcon, u"Share This Page", web_page,
                                 /*active=*/false));
    buttons.push_back(MakeButton(&KoanSitePanelActionsView::Screenshot,
                                 kKoanScreenshotIcon, u"Take a Screenshot",
                                 IsEnabled(IDC_SHARING_HUB_SCREENSHOT), false));
    buttons.push_back(MakeButton(
        &KoanSitePanelActionsView::ReaderMode, kKoanReaderModeIcon,
        u"Enter Reader Mode",
        web_page && IsEnabled(IDC_SHOW_READING_MODE_KEYBOARD), false));
    buttons.push_back(MakeButton(
        &KoanSitePanelActionsView::Bookmark,
        bookmarked ? kKoanBookmarkIcon : kKoanBookmarkHollowIcon,
        u"Bookmark This Page", IsEnabled(IDC_BOOKMARK_THIS_TAB), bookmarked));
    // Zen reverses the row with the sidebar on the left (single toolbar):
    // Bookmark comes first, next to the site icon's side.
    if (!IsSidebarOnRight(browser_view)) {
      std::ranges::reverse(buttons);
    }
    for (std::unique_ptr<KoanSitePanelButton>& button : buttons) {
      SetFlexForView(AddChildView(std::move(button)), 1);
    }
  }

  // views::View:
  void ViewHierarchyChanged(
      const views::ViewHierarchyChangedDetails& details) override {
    views::BoxLayoutView::ViewHierarchyChanged(details);
    // Added to the bubble after it was sized: size it again (page info
    // resizes its bubble when a child's preferred size changes).
    if (details.is_add && details.child == this) {
      PreferredSizeChanged();
    }
  }

 private:
  using Action = void (KoanSitePanelActionsView::*)();

  std::unique_ptr<KoanSitePanelButton> MakeButton(Action action,
                                                  const gfx::VectorIcon& icon,
                                                  std::u16string name,
                                                  bool enabled,
                                                  bool active) {
    auto button = std::make_unique<KoanSitePanelButton>(
        base::BindRepeating(action, base::Unretained(this)), icon,
        std::move(name), active);
    button->SetEnabled(enabled);
    return button;
  }

  bool IsEnabled(int command_id) const {
    return browser_ && chrome::IsCommandEnabled(browser_.get(), command_id);
  }

  // Zen closes the panel for every action but Share on macOS.
  void CloseAndRun(int command_id) {
    base::WeakPtr<BrowserWindowInterface> browser = browser_;
    if (GetWidget()) {
      GetWidget()->Close();
    }
    if (browser) {
      chrome::ExecuteCommand(browser.get(), command_id);
    }
  }

  void Share() {
    if (!contents_) {
      return;
    }
    if (CanSharePage()) {
      SharePage(contents_.get(),
                StripTrackingParameters(contents_->GetVisibleURL()));
      return;
    }
    // Elsewhere Zen copies the URL instead.
    BrowserView* browser_view = browser_ ? browser_view_.get() : nullptr;
    if (GetWidget()) {
      GetWidget()->Close();
    }
    if (browser_view) {
      CopyCurrentUrl(browser_view, /*markdown=*/false);
    }
  }
  void Screenshot() { CloseAndRun(IDC_SHARING_HUB_SCREENSHOT); }
  // Chromium's Reading mode (its side panel), shown or hidden.
  void ReaderMode() { CloseAndRun(IDC_SHOW_READING_MODE_KEYBOARD); }
  // The bookmark bubble, at the pill (koan_url_bar.h GetBubbleAnchorView()).
  void Bookmark() { CloseAndRun(IDC_BOOKMARK_THIS_TAB); }

  base::WeakPtr<BrowserWindowInterface> browser_;
  // Only used while `browser_` is alive (the BrowserView goes with it).
  const raw_ptr<BrowserView, DisableDanglingPtrDetection> browser_view_;
  base::WeakPtr<content::WebContents> contents_;
};

BEGIN_METADATA(KoanSitePanelActionsView)
END_METADATA

}  // namespace

void AddSitePanelActions(views::BubbleDialogDelegateView* bubble,
                         BrowserView* browser_view,
                         content::WebContents* contents) {
  // Page info's bubble is a vertical stack of its pages; the row goes on
  // top, so it stays while a subpage (a permission, cookies) is open.
  if (!bubble || !contents || !browser_view ||
      !views::IsViewClass<PageInfoBubbleView>(bubble)) {
    return;
  }
  bubble->AddChildViewAt(
      std::make_unique<KoanSitePanelActionsView>(browser_view, contents), 0);
}

bool CanSharePage() {
#if BUILDFLAG(IS_MAC)
  return true;
#else
  return false;
#endif
}

void SharePage(content::WebContents* contents, const GURL& url) {
#if BUILDFLAG(IS_MAC)
  content::RenderWidgetHostView* view =
      contents ? contents->GetRenderWidgetHostView() : nullptr;
  if (!view || !url.SchemeIsHTTPOrHTTPS()) {
    return;
  }
  // Zen shares the URL alone (no title, no text).
  view->ShowSharePicker(std::string(), std::string(), url,
                        std::vector<std::string>(), base::DoNothing());
#endif
}

}  // namespace koan
