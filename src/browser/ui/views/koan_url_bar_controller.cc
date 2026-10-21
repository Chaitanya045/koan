// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_url_bar_controller.h"

#include <algorithm>
#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/omnibox/omnibox_controller.h"
#include "chrome/browser/ui/omnibox/omnibox_edit_model.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_compact_mode_controller.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_sidebar_nav_view.h"
#include "koan/browser/ui/views/koan_toast.h"
#include "koan/browser/ui/views/koan_url_bar.h"
#include "ui/actions/actions.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// Zen fades the bar in where it opens. The sizes are in
// koan_sidebar_metrics.h.
constexpr base::TimeDelta kFadeDuration = base::Milliseconds(120);

// Zen's zen.urlbar.wait-to-clear.
constexpr base::TimeDelta kKeepLastSearch = base::Seconds(45);

}  // namespace

// Behind the floating card, over the whole window: paints Zen's shadow of
// the card and closes the URL bar when clicked (Zen: a click outside closes
// it without opening anything).
class KoanUrlBarBackdrop : public views::View {
  METADATA_HEADER(KoanUrlBarBackdrop, views::View)

 public:
  explicit KoanUrlBarBackdrop(base::RepeatingClosure on_press)
      : on_press_(std::move(on_press)) {
    GetViewAccessibility().SetIsIgnored(true);
  }

  void SetCard(const gfx::Rect& card) {
    if (card_ != card) {
      card_ = card;
      SchedulePaint();
    }
  }

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override {
    // Closing removes this view; not while it handles the event.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                             on_press_);
    return true;
  }

  void OnPaint(gfx::Canvas* canvas) override {
    if (card_.IsEmpty()) {
      return;
    }
    const ui::ColorProvider* colors = GetColorProvider();
    const gfx::ShadowValues shadows = {gfx::ShadowValue(
        gfx::Vector2d(0, kFloatingUrlBarShadowY), kFloatingUrlBarShadowBlur,
        colors->GetColor(kColorKoanFloatingUrlBarShadow))};
    // The shape itself, in the card's colour, is hidden under the card.
    gfx::Rect shape = card_;
    shape.Inset(-kFloatingUrlBarShadowSpread);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kColorToolbar));
    flags.setLooper(gfx::CreateShadowDrawLooper(shadows));
    canvas->DrawRoundRect(gfx::RectF(shape), kFloatingUrlBarCornerRadius,
                          flags);
  }

 private:
  base::RepeatingClosure on_press_;
  gfx::Rect card_;
};

BEGIN_METADATA(KoanUrlBarBackdrop)
END_METADATA

KoanUrlBarController::KoanUrlBarController(
    BrowserView* browser_view,
    KoanCompactModeController* compact_mode)
    : browser_view_(browser_view), compact_mode_(compact_mode) {
  InstallNewTabAction();
}

KoanUrlBarController::~KoanUrlBarController() {
  if (views::View* omnibox = key_target_.view()) {
    omnibox->RemovePreTargetHandler(this);
  }
}

void KoanUrlBarController::UpdateKeyHandler() {
  // While the bar is open, or open for a new tab in the toolbar (the
  // multiple-toolbars layout).
  LocationBarView* bar = location_bar();
  views::View* omnibox =
      (is_open() || new_tab_) && bar ? bar->omnibox_view() : nullptr;
  if (key_target_.view() == omnibox) {
    return;
  }
  if (views::View* old_omnibox = key_target_.view()) {
    old_omnibox->RemovePreTargetHandler(this);
  }
  key_target_.SetView(omnibox);
  if (omnibox) {
    omnibox->AddPreTargetHandler(this);
  }
}

void KoanUrlBarController::InstallNewTabAction() {
  BrowserWindowInterface* browser = browser_view_->browser();
  BrowserActions* actions = BrowserActions::From(browser);
  if (!features::IsUrlBarEnabled() || !browser_view_->GetIsNormalType() ||
      !actions || !actions->root_action_item()) {
    return;
  }
  actions::ActionItem* new_tab = actions::ActionManager::Get().FindAction(
      kActionNewTab, actions->root_action_item());
  if (!new_tab) {
    return;
  }
  // The action is the window's own (BrowserActions), so this only changes
  // this window's new-tab button and menu item.
  new_tab->SetInvokeActionCallback(base::BindRepeating(
      [](base::WeakPtr<KoanUrlBarController> controller,
         BrowserWindowInterface* browser, actions::ActionItem* item,
         actions::ActionInvocationContext context) {
        if (controller) {
          controller->OnNewTabAction();
          return;
        }
        chrome::NewTab(browser, NewTabTypes::kNewTabCommand);
      },
      weak_factory_.GetWeakPtr(), base::Unretained(browser)));
}

void KoanUrlBarController::OnNewTabAction() {
  if (!OpenForNewTab()) {
    chrome::NewTab(browser_view_->browser(), NewTabTypes::kNewTabCommand);
  }
}

bool KoanUrlBarController::OpenForNewTab() {
  LocationBarView* bar = location_bar();
  if (!features::IsUrlBarEnabled() || !browser_view_->GetIsNormalType() ||
      browser_view_->IsFullscreen() ||
      browser_view_->browser()->GetTabStripModel()->empty() || !bar ||
      !bar->omnibox_view() ||
      !browser_view_->GetProfile()
           ->GetOriginalProfile()
           ->GetPrefs()
           ->GetBoolean(prefs::kUrlBarReplaceNewTab)) {
    return false;
  }
  // Zen: the shortcut again closes the bar it opened.
  if (new_tab_) {
    Close();
    return true;
  }
  new_tab_ = true;
  UpdateKeyHandler();
  std::u16string text;
  if (base::TimeTicks::Now() - last_search_time_ < kKeepLastSearch) {
    text = last_search_;
  }
  last_search_.clear();
  bar->FocusLocation(/*is_user_initiated=*/true,
                     /*clear_focus_if_failed=*/false);
  if (!bar->omnibox_view()->HasFocus()) {
    new_tab_ = false;
    UpdateKeyHandler();
    return false;
  }
  // Already focused before (no focus change): place it for the new tab.
  if (is_open()) {
    SetPlacement(DesiredPlacement());
  }
  // Zen opens the new-tab bar empty (or with the unfinished search).
  bar->omnibox_view()->SetUserText(text, /*update_popup=*/true);
  return true;
}

void KoanUrlBarController::Close() {
  LocationBarView* bar = location_bar();
  if (views::WebView* web_view = browser_view_->GetActiveContentsWebView()) {
    web_view->RequestFocus();
  }
  views::FocusManager* focus_manager = browser_view_->GetFocusManager();
  if (bar && focus_manager && focus_manager->GetFocusedView() &&
      bar->Contains(focus_manager->GetFocusedView())) {
    focus_manager->ClearFocus();
  }
}

void KoanUrlBarController::OnBrowserViewLayout() {
  if (!is_open()) {
    return;
  }
  if (DesiredPlacement() != placement_) {
    // Placements change views; not during layout.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanUrlBarController::OnModeChanged,
                                  weak_factory_.GetWeakPtr()));
    return;
  }
  Layout();
}

void KoanUrlBarController::OnModeChanged() {
  views::FocusManager* focus_manager = browser_view_->GetFocusManager();
  LocationBarView* bar = location_bar();
  const bool editing = bar && focus_manager &&
                       focus_manager->GetFocusedView() &&
                       bar->Contains(focus_manager->GetFocusedView());
  SetPlacement(editing ? DesiredPlacement() : Placement::kNone);
}

void KoanUrlBarController::OnFocusChanged(views::View* focused_now) {
  // The focus leaves with the window (Alt-Tab, another window) and comes
  // back with it: keep the bar, what was typed and the new-tab session, like
  // Zen. Views clears the focus after the widget is inactive.
  views::Widget* widget = browser_view_->GetWidget();
  if (!focused_now && widget && !widget->IsActive()) {
    return;
  }
  LocationBarView* bar = location_bar();
  if (focused_now && bar && bar->Contains(focused_now)) {
    if (!is_open()) {
      user_input_in_progress_ =
          bar->GetOmniboxController()->edit_model()->user_input_in_progress();
    }
    SetPlacement(DesiredPlacement());
    return;
  }
  const bool was_open = is_open() || new_tab_;
  EndNewTabSession(/*committed=*/false);
  EndEditing();
  // Like Zen's single toolbar, a closed bar shows the page's address again
  // (the pill shows it; the omnibox is out of sight).
  if (was_open && bar) {
    bar->Revert();
  }
}

void KoanUrlBarController::EndEditing() {
  SetPlacement(Placement::kNone);
  floated_ = false;
  user_input_in_progress_ = false;
}

void KoanUrlBarController::OnOmniboxChanged(bool user_input_in_progress) {
  if ((!is_open() && !new_tab_) ||
      user_input_in_progress == user_input_in_progress_) {
    return;
  }
  user_input_in_progress_ = user_input_in_progress;
  // Called from LocationBarView::OnChanged(): move the bar afterwards.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanUrlBarController::FloatOnType,
                                weak_factory_.GetWeakPtr()));
}

void KoanUrlBarController::FloatOnType() {
  if (!is_open()) {
    return;
  }
  SetPlacement(DesiredPlacement());
  // "normal" widens over the page while typing.
  Layout();
}

bool KoanUrlBarController::TakeNewTabDisposition() {
  if (!new_tab_) {
    return false;
  }
  EndNewTabSession(/*committed=*/true);
  return true;
}

void KoanUrlBarController::EndNewTabSession(bool committed) {
  if (!new_tab_) {
    return;
  }
  new_tab_ = false;
  UpdateKeyHandler();
  LocationBarView* bar = location_bar();
  if (!committed && user_input_in_progress_ && bar && bar->omnibox_view()) {
    last_search_ = bar->omnibox_view()->GetText();
    last_search_time_ = base::TimeTicks::Now();
  }
}

std::string KoanUrlBarController::Behavior() const {
  return browser_view_->GetProfile()
      ->GetOriginalProfile()
      ->GetPrefs()
      ->GetString(prefs::kUrlBarBehavior);
}

LocationBarView* KoanUrlBarController::location_bar() const {
  return browser_view_->GetLocationBarView();
}

bool KoanUrlBarController::IsPillAvailable() const {
  KoanSidebarNavView* nav = compact_mode_->sidebar_nav();
  return nav && nav->url_pill()->IsDrawn() &&
         !nav->url_pill()->GetVisibleBounds().IsEmpty();
}

KoanUrlBarController::Placement KoanUrlBarController::DesiredPlacement() const {
  // In the multiple-toolbars layout the omnibox edits in the toolbar.
  if (!compact_mode_->hides_toolbar() || browser_view_->IsFullscreen()) {
    return Placement::kNone;
  }
  // Without the Koan URL bar, Koan's earlier behaviour: always floating.
  if (!features::IsUrlBarEnabled()) {
    return Placement::kFloating;
  }
  // Compact mode with the sidebar hidden floats it too when it's opened from
  // a peeked sidebar's pill: Chromium collapses an expanded-on-hover sidebar
  // while the omnibox's suggestions are open, which would take the pill away.
  const std::string behavior = Behavior();
  if (floated_ || behavior == prefs::kUrlBarBehaviorFloat ||
      compact_mode_->hides_sidebar() || !IsPillAvailable()) {
    return Placement::kFloating;
  }
  // Zen's floating-on-type: the first keystroke floats the bar, and the
  // new-tab bar (opened by keyboard, empty) floats right away.
  if (behavior != prefs::kUrlBarBehaviorNormal &&
      (user_input_in_progress_ || new_tab_)) {
    return Placement::kFloating;
  }
  return Placement::kInPill;
}

void KoanUrlBarController::SetPlacement(Placement placement) {
  if (placement == placement_) {
    return;
  }
  views::View* top_container = browser_view_->top_container();
  LocationBarView* bar = location_bar();
  const bool opening = placement_ == Placement::kNone;
  if (placement == Placement::kNone || !bar || !bar->parent() ||
      !top_container->layer() || !top_container->parent() ||
      !browser_view_->toolbar()) {
    if (opening) {
      return;
    }
    // Closing: put everything back.
    if (KoanSidebarNavView* nav = compact_mode_->sidebar_nav()) {
      nav->SetEditing(false);
    }
    top_container_observation_.Reset();
    toolbar_observation_.Reset();
    sibling_observation_.RemoveAllObservations();
    {
      base::AutoReset<bool> changing(&changing_toolbar_visibility_, true);
      for (const auto& [view, visible] : restore_) {
        view->SetVisible(visible);
      }
    }
    restore_.clear();
    card_size_ = gfx::Size();
    if (ui::Layer* layer = top_container->layer()) {
      layer->GetAnimator()->AbortAllAnimations();
      layer->SetOpacity(1.0f);
      layer->SetRoundedCornerRadius(gfx::RoundedCornersF());
      layer->SetMasksToBounds(false);
    }
    placement_ = Placement::kNone;
    UpdateKeyHandler();
    ApplyCardStyle();
    browser_view_->InvalidateLayout();
    compact_mode_->OnUrlBarOpenChanged();
    return;
  }

  placement_ = placement;
  if (placement == Placement::kFloating) {
    floated_ = true;
  }
  if (opening) {
    if (KoanSidebarNavView* nav = compact_mode_->sidebar_nav()) {
      nav->SetEditing(true);
    }
    // Just the location bar: hide the rest of the toolbar, remembering what
    // to restore (and watching for other code showing things meanwhile).
    for (views::View* sibling : bar->parent()->children()) {
      if (sibling != bar) {
        restore_[sibling] = sibling->GetVisible();
        sibling_observation_.AddObservation(sibling);
      }
    }
    HideToolbarSiblings();
    top_container->SetCanProcessEventsWithinSubtree(true);
    top_container_observation_.Observe(top_container);
    toolbar_observation_.Observe(browser_view_->toolbar());
    UpdateKeyHandler();
    ui::Layer* layer = top_container->layer();
    layer->GetAnimator()->AbortAllAnimations();
    layer->SetTransform(gfx::Transform());
    compact_mode_->OnUrlBarOpenChanged();
  }
  // The style first: the larger floating font can change the toolbar's
  // preferred height.
  ApplyCardStyle();
  Layout();
  // Zen fades the bar in where it opens; moving from the pill to floating
  // keeps what was just typed in view.
  if (opening) {
    top_container->layer()->SetOpacity(0.0f);
    views::AnimationBuilder()
        .Once()
        .SetDuration(kFadeDuration)
        .SetOpacity(top_container, 1.0f, gfx::Tween::EASE_OUT);
  }
}

void KoanUrlBarController::HideToolbarSiblings() {
  if (!is_open()) {
    return;
  }
  base::AutoReset<bool> changing(&changing_toolbar_visibility_, true);
  for (const auto& [view, visible] : restore_) {
    view->SetVisible(false);
  }
}

void KoanUrlBarController::ApplyCardStyle() {
  const bool floating = placement_ == Placement::kFloating;
  LocationBarView* bar = location_bar();
  OmniboxViewViews* omnibox = bar ? bar->omnibox_view() : nullptr;

  // Zen's floating bar has a larger font than the toolbar's.
  if (floating && !omnibox_font_ && omnibox) {
    omnibox_font_ = omnibox->GetFontList();
    omnibox->SetFontList(
        omnibox_font_->DeriveWithSizeDelta(kFloatingUrlBarFontDelta));
  } else if (!floating && omnibox_font_) {
    if (omnibox) {
      omnibox->SetFontList(*omnibox_font_);
    }
    omnibox_font_.reset();
  }

  views::View* top_container = browser_view_->top_container();
  if (floating && !backdrop_ && top_container->parent()) {
    views::View* overlay = top_container->parent();
    backdrop_ = overlay->AddChildViewAt(
        std::make_unique<KoanUrlBarBackdrop>(base::BindRepeating(
            &KoanUrlBarController::Close, weak_factory_.GetWeakPtr())),
        overlay->GetIndexOf(top_container).value());
    backdrop_->SetPaintToLayer();
    backdrop_->layer()->SetFillsBoundsOpaquely(false);
    backdrop_->layer()->SetOpacity(0.0f);
    views::AnimationBuilder()
        .Once()
        .SetDuration(kFadeDuration)
        .SetOpacity(backdrop_, 1.0f, gfx::Tween::EASE_OUT);
  } else if (!floating && backdrop_) {
    views::View* overlay = backdrop_->parent();
    overlay->RemoveChildViewT(std::exchange(backdrop_, nullptr));
  }

  if (!is_open()) {
    return;
  }
  ui::Layer* layer = top_container->layer();
  layer->SetRoundedCornerRadius(gfx::RoundedCornersF(
      floating ? kFloatingUrlBarCornerRadius : kUrlPillCornerRadius));
  layer->SetIsFastRoundedCorner(true);
  layer->SetMasksToBounds(true);
}

void KoanUrlBarController::Layout() {
  views::View* top_container = browser_view_->top_container();
  ToolbarView* toolbar = browser_view_->toolbar();
  if (!is_open() || laying_out_ || !toolbar || !top_container->parent()) {
    return;
  }
  base::AutoReset<bool> laying_out(&laying_out_, true);
  const gfx::Rect card = CardBounds();
  if (card.IsEmpty()) {
    // The pill went away (e.g. the sidebar collapsed); place it again.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanUrlBarController::OnModeChanged,
                                  weak_factory_.GetWeakPtr()));
    return;
  }
  // Zen's card is the URL field itself: size the card to the location bar
  // and shift the toolbar so the location bar fills it (the rounded mask
  // clips the toolbar's padding). Lay the toolbar out at the card's width
  // first to find where it puts the location bar.
  LocationBarView* bar = location_bar();
  if (!bar) {
    return;
  }
  toolbar->SetBoundsRect(gfx::Rect(card.size()));
  toolbar->DeprecatedLayoutImmediately();
  const gfx::Rect field =
      views::View::ConvertRectToTarget(bar->parent(), toolbar, bar->bounds());
  const int right_padding = toolbar->width() - field.right();
  const gfx::Rect bar_card(card.x(),
                           card.CenterPoint().y() - field.height() / 2,
                           card.width(), field.height());
  top_container->SetBoundsRect(bar_card);
  toolbar->SetBoundsRect(gfx::Rect(-field.x(), -field.y(),
                                   card.width() + field.x() + right_padding,
                                   card.height()));
  toolbar->DeprecatedLayoutImmediately();
  if (card_size_ != bar_card.size()) {
    card_size_ = bar_card.size();
    // BrowserView's layout clips the top container and the toolbar to their
    // size at the end of each pass (BrowserViewLayoutImpl, crbug.com/
    // 390669712). A size change outside that pass (floating out of the pill
    // as you type) needs another one, or the card stays clipped.
    browser_view_->InvalidateLayout();
  }
  if (backdrop_) {
    backdrop_->SetBoundsRect(top_container->parent()->GetLocalBounds());
    backdrop_->SetCard(bar_card);
  }
}

gfx::Rect KoanUrlBarController::CardBounds() const {
  views::View* overlay = browser_view_->top_container()->parent();
  const int height = browser_view_->toolbar()->GetPreferredSize().height();
  if (height <= 0) {
    return gfx::Rect();
  }
  const gfx::Rect area = views::View::ConvertRectToTarget(
      browser_view_, overlay, browser_view_->GetLocalBounds());
  if (placement_ == Placement::kFloating) {
    const int width = std::min(area.width() * 2 / 3, kFloatingUrlBarMaxWidth);
    const int centred_height = std::max(kFloatingUrlBarMinHeight, height);
    const int top =
        area.y() + std::max(0, (area.height() - centred_height) / 2);
    return gfx::Rect(area.x() + (area.width() - width) / 2, top, width, height);
  }
  KoanSidebarNavView* nav = compact_mode_->sidebar_nav();
  if (!nav || !IsPillAvailable()) {
    return gfx::Rect();
  }
  views::View* pill = nav->url_pill();
  const gfx::Rect pill_bounds =
      views::View::ConvertRectToTarget(pill, overlay, pill->GetLocalBounds());
  // "normal" widens it over the page, away from the sidebar's edge.
  const bool sidebar_on_right = IsSidebarOnRight(browser_view_);
  int width = pill_bounds.width();
  if (user_input_in_progress_) {
    const int room = sidebar_on_right ? pill_bounds.right() - area.x()
                                      : area.right() - pill_bounds.x();
    width = std::max(
        width, std::min({area.width() * 9 / 10, kUrlBarOpenMaxWidth, room}));
  }
  // Over the pill, keeping its centre, so it reads as the pill being edited.
  return gfx::Rect(
      sidebar_on_right ? pill_bounds.right() - width : pill_bounds.x(),
      pill_bounds.CenterPoint().y() - height / 2, width, height);
}

void KoanUrlBarController::OnViewVisibilityChanged(views::View* observed_view,
                                                   views::View* starting_view,
                                                   bool visible) {
  // A toolbar view that other code shows while the bar is open: it should
  // be visible afterwards, but not inside the card.
  auto it = restore_.find(observed_view);
  if (observed_view != starting_view || it == restore_.end() ||
      changing_toolbar_visibility_ || !visible) {
    return;
  }
  it->second = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanUrlBarController::HideToolbarSiblings,
                                weak_factory_.GetWeakPtr()));
}

void KoanUrlBarController::OnViewBoundsChanged(views::View* observed_view) {
  // BrowserView's layout puts the top container back at full width.
  Layout();
}

void KoanUrlBarController::OnViewIsDeleting(views::View* observed_view) {
  if (restore_.erase(observed_view)) {
    sibling_observation_.RemoveObservation(observed_view);
  }
  if (top_container_observation_.IsObservingSource(observed_view)) {
    top_container_observation_.Reset();
  }
  if (toolbar_observation_.IsObservingSource(observed_view)) {
    toolbar_observation_.Reset();
  }
}

void KoanUrlBarController::OnKeyEvent(ui::KeyEvent* event) {
  // Zen: Esc closes the bar (Chrome's omnibox would first close the
  // suggestions, then revert, then give the page focus).
  if (event->type() == ui::EventType::kKeyPressed &&
      event->key_code() == ui::VKEY_ESCAPE && !event->IsShiftDown() &&
      (is_open() || new_tab_)) {
    event->SetHandled();
    // Not while the omnibox dispatches the key to this handler.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&KoanUrlBarController::Close,
                                  weak_factory_.GetWeakPtr()));
  }
}

void CopyCurrentUrl(BrowserView* browser_view, bool markdown) {
  content::WebContents* contents =
      browser_view->browser()->GetTabStripModel()->GetActiveWebContents();
  if (!contents) {
    return;
  }
  const std::u16string url =
      base::UTF8ToUTF16(contents->GetVisibleURL().spec());
  std::u16string text = url;
  if (markdown) {
    std::u16string title = contents->GetTitle();
    if (title.empty()) {
      title = url;
    }
    text = base::StrCat({u"[", title, u"](", url, u")"});
  }
  {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteText(text);
  }
  // Zen's strings (zen-general.ftl).
  if (KoanBrowserViewController* controller =
          KoanBrowserViewController::From(browser_view)) {
    controller->toast()->Show(markdown ? u"Copied current URL as Markdown!"
                                       : u"Copied current URL!");
  }
}

// The koan_url_bar.h hooks.

void MaybeAdjustOmniboxDisposition(BrowserWindowInterface* browser,
                                   WindowOpenDisposition* disposition) {
  if (!browser || *disposition != WindowOpenDisposition::CURRENT_TAB) {
    return;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  if (controller && controller->url_bar()->TakeNewTabDisposition()) {
    *disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  }
}

bool MaybeOpenUrlBarForNewTab(BrowserWindowInterface* browser) {
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  return controller && controller->url_bar()->OpenForNewTab();
}

void OnOmniboxChanged(views::View* location_bar, bool user_input_in_progress) {
  views::Widget* widget = location_bar->GetWidget();
  BrowserView* browser_view = widget
                                  ? BrowserView::GetBrowserViewForNativeWindow(
                                        widget->GetNativeWindow())
                                  : nullptr;
  KoanBrowserViewController* controller =
      browser_view ? KoanBrowserViewController::From(browser_view) : nullptr;
  if (controller) {
    controller->url_bar()->OnOmniboxChanged(user_input_in_progress);
  }
}

}  // namespace koan
