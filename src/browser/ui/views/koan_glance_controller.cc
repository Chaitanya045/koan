// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_glance_controller.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_glance.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/mojom/frame/triggering_event_info.mojom-shared.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/transform_util.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

constexpr float kCardCornerRadius = 10;
constexpr int kButtonSize = 32;
constexpr int kButtonGap = 8;
constexpr base::TimeDelta kOpenDuration = base::Milliseconds(200);
// Scrim over the page behind the card.
constexpr SkColor kScrimColor = SkColorSetA(SK_ColorBLACK, 0x66);

}  // namespace

// The overlay: a dimmed backdrop over the contents area, the card holding the
// Glance page, and a column of buttons beside the card.
class KoanGlanceView : public views::View {
  METADATA_HEADER(KoanGlanceView, views::View)

 public:
  KoanGlanceView(KoanGlanceController* controller,
                 content::WebContents* contents)
      : controller_(controller) {
    scrim_ = AddChildView(std::make_unique<views::View>());
    // A textured (not solid-colour) layer, so its opacity can animate.
    scrim_->SetPaintToLayer();
    scrim_->layer()->SetFillsBoundsOpaquely(false);
    scrim_->SetBackground(views::CreateSolidBackground(kScrimColor));
    scrim_->SetCanProcessEventsWithinSubtree(false);

    card_ = AddChildView(std::make_unique<views::View>());
    card_->SetPaintToLayer();
    card_->layer()->SetFillsBoundsOpaquely(false);
    card_->layer()->SetRoundedCornerRadius(
        gfx::RoundedCornersF(kCardCornerRadius));
    card_->SetLayoutManager(std::make_unique<views::FillLayout>());
    web_view_ = card_->AddChildView(std::make_unique<views::WebView>(
        contents->GetBrowserContext()));
    web_view_->SetWebContents(contents);

    buttons_ = AddChildView(std::make_unique<views::BoxLayoutView>());
    buttons_->SetOrientation(views::BoxLayout::Orientation::kVertical);
    buttons_->SetBetweenChildSpacing(kButtonGap);
    AddButton(vector_icons::kCloseIcon, u"Close (Esc)",
              base::BindRepeating(&KoanGlanceController::PostAction,
                                  base::Unretained(controller_),
                                  &KoanGlanceController::Close));
    AddButton(kOpenInNewIcon, u"Expand to tab",
              base::BindRepeating(&KoanGlanceController::PostAction,
                                  base::Unretained(controller_),
                                  &KoanGlanceController::ExpandToTab));
    AddButton(kSplitSceneIcon, u"Open in split view",
              base::BindRepeating(&KoanGlanceController::PostAction,
                                  base::Unretained(controller_),
                                  &KoanGlanceController::OpenInSplit));
  }

  views::WebView* web_view() { return web_view_; }

  void PlayOpenAnimation() {
    gfx::Transform from = gfx::GetScaleTransform(
        gfx::Rect(card_->size()).CenterPoint(), 0.94f);
    card_->layer()->SetOpacity(0.f);
    card_->layer()->SetTransform(from);
    scrim_->layer()->SetOpacity(0.f);
    views::AnimationBuilder()
        .Once()
        .SetDuration(kOpenDuration)
        .SetOpacity(card_, 1.f, gfx::Tween::EASE_OUT)
        .SetTransform(card_, gfx::Transform(), gfx::Tween::EASE_OUT)
        .SetOpacity(scrim_, 1.f, gfx::Tween::EASE_OUT);
  }

  // views::View:
  void Layout(PassKey) override {
    const gfx::Rect bounds = GetLocalBounds();
    scrim_->SetBoundsRect(bounds);
    // Zen-like proportions: most of the width, a little air above and below,
    // and room on the trailing side for the button column.
    const int side = std::max(48, bounds.width() / 12);
    const int vertical = std::max(24, bounds.height() / 24);
    gfx::Rect card = bounds;
    card.Inset(gfx::Insets::TLBR(vertical, side, vertical, side));
    card_->SetBoundsRect(card);
    const gfx::Size buttons_size = buttons_->GetPreferredSize();
    buttons_->SetBounds(card.right() + kButtonGap, card.y(),
                        buttons_size.width(), buttons_size.height());
    web_view_->holder()->SetNativeViewCornerRadii(
        gfx::RoundedCornersF(kCardCornerRadius));
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    // Only the dimmed backdrop gets here; the card and buttons take their
    // own events.
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    controller_->PostAction(&KoanGlanceController::Close);
  }

 private:
  void AddButton(const gfx::VectorIcon& icon,
                 std::u16string tooltip,
                 views::Button::PressedCallback callback) {
    auto button = views::CreateVectorImageButtonWithNativeTheme(
        std::move(callback), icon, /*dip_size=*/18);
    button->SetTooltipText(tooltip);
    button->GetViewAccessibility().SetName(tooltip);
    button->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
    // Koan's surface colour (the sidebar/toolbar), so the buttons read as
    // part of the browser chrome against the dimmed page.
    button->SetBackground(
        views::CreateRoundedRectBackground(kColorToolbar, kButtonSize / 2));
    buttons_->AddChildView(std::move(button));
  }

  const raw_ptr<KoanGlanceController> controller_;
  raw_ptr<views::View> scrim_ = nullptr;
  raw_ptr<views::View> card_ = nullptr;
  raw_ptr<views::WebView> web_view_ = nullptr;
  raw_ptr<views::BoxLayoutView> buttons_ = nullptr;
};

BEGIN_METADATA(KoanGlanceView)
END_METADATA

bool MaybeOpenGlance(BrowserWindowInterface* browser,
                     content::WebContents* source,
                     const content::OpenURLParams& params) {
  if (!features::IsGlanceEnabled() || !browser || !source) {
    return false;
  }
  // A real Shift+click on a link: a renderer-initiated new-window navigation
  // from a trusted user gesture. Context-menu "Open link in new window" is
  // browser-initiated and scripted window.open() has no trusted gesture, so
  // both keep their usual behaviour.
  if (params.disposition != WindowOpenDisposition::NEW_WINDOW ||
      !params.is_renderer_initiated || !params.user_gesture ||
      params.triggering_event_info !=
          blink::mojom::TriggeringEventInfo::kFromTrustedEvent ||
      !params.url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  if (browser->GetTabStripModel()->GetActiveWebContents() != source) {
    return false;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !browser_view->GetIsNormalType()) {
    return false;
  }
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view);
  if (!controller) {
    return false;
  }
  controller->glance()->Open(params, source);
  return true;
}

KoanGlanceController::KoanGlanceController(BrowserView* browser_view)
    : browser_view_(browser_view) {
  browser_view_->browser()->GetTabStripModel()->AddObserver(this);
}

KoanGlanceController::~KoanGlanceController() = default;

void KoanGlanceController::Open(const content::OpenURLParams& params,
                                content::WebContents* source) {
  if (is_open()) {
    Close();
  }
  source_ = source;

  content::WebContents::CreateParams create_params(
      browser_view_->GetProfile());
  contents_ = content::WebContents::Create(create_params);
  contents_->SetDelegate(this);
  contents_->GetController().LoadURLWithParams(
      content::NavigationController::LoadURLParams(params));

  // A child widget over the contents area, so the overlay has its own native
  // window above the page's and receives the pointer everywhere over it.
  views::Widget::InitParams widget_params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  widget_params.parent = browser_view_->GetWidget()->GetNativeView();
  widget_params.opacity =
      views::Widget::InitParams::WindowOpacity::kTranslucent;
  widget_params.name = "KoanGlance";
  widget_ = std::make_unique<views::Widget>();
  widget_->Init(std::move(widget_params));
  view_ = widget_->SetContentsView(
      std::make_unique<KoanGlanceView>(this, contents_.get()));
  LayoutWidget();
  widget_->Show();
  view_->PlayOpenAnimation();
  view_->web_view()->RequestFocus();
}

void KoanGlanceController::PostAction(
    void (KoanGlanceController::*action)()) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(action, weak_factory_.GetWeakPtr()));
}

void KoanGlanceController::Close() {
  if (!is_open()) {
    return;
  }
  DestroyWidget();
  // Close() can run from inside one of the Glance WebContents' own delegate
  // callbacks (Esc, window.close()), so destroy it once the stack unwinds.
  contents_->SetDelegate(nullptr);
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, std::move(contents_));
  if (source_) {
    source_->Focus();
  }
  source_ = nullptr;
}

std::unique_ptr<content::WebContents> KoanGlanceController::TakeContents() {
  DestroyWidget();
  contents_->SetDelegate(nullptr);
  source_ = nullptr;
  return std::move(contents_);
}

void KoanGlanceController::ExpandToTab() {
  if (!is_open()) {
    return;
  }
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  const int source_index = model->GetIndexOfWebContents(source_);
  const std::optional<tab_groups::TabGroupId> group =
      source_index != TabStripModel::kNoTab
          ? model->GetTabGroupForTab(source_index)
          : std::nullopt;
  const int index =
      source_index != TabStripModel::kNoTab ? source_index + 1 : model->count();
  model->InsertWebContentsAt(
      index, TakeContents(),
      AddTabTypes::ADD_ACTIVE | AddTabTypes::ADD_INHERIT_OPENER, group);
}

void KoanGlanceController::OpenInSplit() {
  if (!is_open()) {
    return;
  }
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  const int source_index = model->GetIndexOfWebContents(source_);
  if (source_index == TabStripModel::kNoTab ||
      model->GetTabAtIndex(source_index)->IsSplit()) {
    // Already split: just open it as a tab next to the split.
    ExpandToTab();
    return;
  }
  const std::optional<tab_groups::TabGroupId> group =
      model->GetTabGroupForTab(source_index);
  // Insert without activating: AddToNewSplit() pairs the given tabs with the
  // active one (the source).
  const int index = model->InsertWebContentsAt(
      source_index + 1, TakeContents(), AddTabTypes::ADD_INHERIT_OPENER, group);
  model->AddToNewSplit(
      {index},
      split_tabs::SplitTabVisualData(split_tabs::SplitTabLayout::kSideBySide),
      split_tabs::SplitTabCreatedSource::kLinkClick);
}

void KoanGlanceController::OnBrowserViewLayout() {
  if (is_open()) {
    LayoutWidget();
  }
}

void KoanGlanceController::OnBrowserViewDestroying() {
  browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
  DestroyWidget();
  if (contents_) {
    contents_->SetDelegate(nullptr);
    contents_.reset();
  }
  source_ = nullptr;
}

void KoanGlanceController::DestroyWidget() {
  view_ = nullptr;
  if (widget_) {
    widget_->CloseNow();
    widget_.reset();
  }
}

void KoanGlanceController::LayoutWidget() {
  if (!widget_) {
    return;
  }
  MultiContentsView* contents_view = browser_view_->multi_contents_view();
  widget_->SetBounds(
      contents_view->ConvertRectToWidget(contents_view->GetLocalBounds()));
}

content::WebContents* KoanGlanceController::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>
        navigation_handle_callback) {
  if (params.disposition == WindowOpenDisposition::CURRENT_TAB) {
    source->GetController().LoadURLWithParams(
        content::NavigationController::LoadURLParams(params));
    return source;
  }
  // New tabs/windows from the Glance page open as usual, from the tab
  // underneath.
  if (source_ && source_->GetDelegate()) {
    return source_->GetDelegate()->OpenURLFromTab(
        source_, params, std::move(navigation_handle_callback));
  }
  return nullptr;
}

content::WebContents* KoanGlanceController::AddNewContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents> new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture,
    bool* was_blocked) {
  if (source_ && source_->GetDelegate()) {
    return source_->GetDelegate()->AddNewContents(
        source_, std::move(new_contents), target_url, disposition,
        window_features, user_gesture, was_blocked);
  }
  return nullptr;
}

void KoanGlanceController::CloseContents(content::WebContents* source) {
  // window.close() arrives mid renderer message; tear down afterwards.
  if (source == contents_.get()) {
    PostAction(&KoanGlanceController::Close);
  }
}

bool KoanGlanceController::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  if (event.GetType() == blink::WebInputEvent::Type::kRawKeyDown &&
      event.windows_key_code == ui::VKEY_ESCAPE) {
    // The key event is still being dispatched to the overlay's own window;
    // destroy it once that unwinds.
    PostAction(&KoanGlanceController::Close);
    return true;
  }
  // Everything else (Ctrl+T, Ctrl+W, ...) behaves as in a tab.
  return browser_view_->HandleKeyboardEvent(event);
}

void KoanGlanceController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // Glance belongs to the tab it was opened from: close it when another tab
  // becomes active or the source tab leaves this tab strip.
  if (!is_open()) {
    return;
  }
  bool source_removed = false;
  if (change.type() == TabStripModelChange::kRemoved) {
    for (const auto& removed : change.GetRemove()->contents) {
      source_removed |= removed.contents == source_;
    }
  }
  if (source_removed) {
    // The source is going away: drop the pointer now, close right after.
    source_ = nullptr;
  }
  if (source_removed || selection.active_tab_changed()) {
    PostAction(&KoanGlanceController::Close);
  }
}

}  // namespace koan
