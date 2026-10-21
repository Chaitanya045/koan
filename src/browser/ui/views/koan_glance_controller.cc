// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_glance_controller.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "components/blocked_content/popup_blocker_tab_helper.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/javascript_dialogs/app_modal_dialog_manager.h"
#include "components/prefs/pref_service.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/tab_interface.h"
#include "components/user_prefs/user_prefs.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/context_menu_params.h"
#include "content/public/browser/focused_node_details.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_content_card.h"
#include "koan/browser/ui/views/koan_glance.h"
#include "koan/browser/ui/views/koan_split_view.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/blink/public/mojom/frame/triggering_event_info.mojom-shared.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/layer_owner.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/origin.h"

#if defined(USE_AURA)
#include "ui/aura/window.h"
#endif

namespace koan {

namespace {

// zen-glance.css: the card is 80% of the contents width and their full
// height, with the content card's radius and Zen's --zen-big-shadow (0 3px
// 8px); the button column hangs off its right edge, 15px down plus 12px
// padding, 34px round buttons 12px apart. Colours are Koan colour ids
// (koan_color_id.h), radii the content card's and the LayoutProvider's, so a
// theme can restyle them.
constexpr float kCardWidthFraction = 0.8f;
constexpr int kButtonSize = 34;
constexpr int kButtonIconSize = 18;
constexpr int kButtonGap = 12;
constexpr int kButtonsTop = 15 + 12;
constexpr gfx::Vector2d kCardShadowOffset(0, 3);
constexpr int kCardShadowBlur = 8;

// Zen's zen.glance.animation-duration. Similar easing to Zen's: the card
// grows out of the click (Zen: ease-in-out over an ease-out-back curve, with
// a slight overshoot Koan doesn't have) and shrinks back into it (Zen:
// ease-out over an ease-out-cubic curve), with the page fading in during the
// first quarter of the growth and out in Zen's 0.15s fade-out.
constexpr base::TimeDelta kAnimationDuration = base::Milliseconds(350);
constexpr base::TimeDelta kPageFadeInDuration = kAnimationDuration / 4;
constexpr base::TimeDelta kPageFadeOutDuration = base::Milliseconds(150);
constexpr gfx::Tween::Type kGrowTween = gfx::Tween::ACCEL_40_DECEL_100_3;
constexpr gfx::Tween::Type kShrinkTween = gfx::Tween::ACCEL_LIN_DECEL_100;
// Zen fades the page behind in with the card (and back in 1/1.5 of the
// duration), and the buttons in and out in 0.2s.
constexpr base::TimeDelta kBackgroundRestoreDuration =
    kAnimationDuration * 2 / 3;
constexpr base::TimeDelta kButtonsFadeDuration = base::Milliseconds(200);
// Zen's close button stays in its "Confirm" state for 3s.
constexpr base::TimeDelta kConfirmDuration = base::Seconds(3);

// The card the Glance grows out of, as a fraction of the full card (Zen
// starts from the clicked link's own rect).
constexpr float kGrowFraction = 0.1f;

// Maps `bounds` onto `rect` (the page window's own coordinates start at
// `bounds`' origin).
gfx::Transform MapBoundsToRect(const gfx::Rect& bounds, const gfx::Rect& rect) {
  gfx::Transform transform;
  transform.Translate(rect.x() - bounds.x(), rect.y() - bounds.y());
  transform.Scale(static_cast<float>(rect.width()) / bounds.width(),
                  static_cast<float>(rect.height()) / bounds.height());
  return transform;
}

const PrefService* PrefsFor(content::WebContents* contents) {
  // Incognito's prefs read through to the regular profile's.
  return user_prefs::UserPrefs::Get(contents->GetBrowserContext());
}

// Glance on in chrome://flags and in Koan's settings.
bool IsGlanceOn(content::WebContents* contents) {
  return features::IsGlanceEnabled() &&
         PrefsFor(contents)->GetBoolean(prefs::kGlanceEnabled);
}

// Marks a Glance page with its controller, so its own context menu (and
// activation clicks) find the Glance it is in.
const void* const kGlancePageKey = &kGlancePageKey;

class GlancePage : public base::SupportsUserData::Data {
 public:
  explicit GlancePage(KoanGlanceController* controller)
      : controller_(controller) {}
  KoanGlanceController* controller() const { return controller_; }

 private:
  const raw_ptr<KoanGlanceController> controller_;
};

// The Glance controller for `contents`: the one of the window it is a tab
// of, or the one showing it as a Glance page. Null with Glance off, for any
// other WebContents (DevTools, extension popups, ...) and outside a normal
// window.
KoanGlanceController* GlanceControllerFor(content::WebContents* contents) {
  if (!contents || !IsGlanceOn(contents)) {
    return nullptr;
  }
  if (auto* page =
          static_cast<GlancePage*>(contents->GetUserData(kGlancePageKey))) {
    return page->controller();
  }
  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(contents);
  if (!browser) {
    return nullptr;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !browser_view->GetIsNormalType()) {
    return nullptr;
  }
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view);
  return controller ? controller->glance() : nullptr;
}

// What a click with the trigger modifier means without Glance (blink's
// NavigationPolicyFromEventModifiers): Ctrl (Cmd on macOS) opens a
// background tab, Shift a window; Alt downloads, which can't be recovered
// here, so the link opens in place. A renderer started before a trigger
// change may still use the old modifier; this follows the current one.
WindowOpenDisposition FallbackDisposition(const PrefService* pref_service) {
  const std::string& method =
      pref_service->GetString(prefs::kGlanceActivationMethod);
  if (method == "shift") {
    return WindowOpenDisposition::NEW_WINDOW;
  }
#if BUILDFLAG(IS_MAC)
  if (method == "meta") {
    return WindowOpenDisposition::NEW_BACKGROUND_TAB;
  }
#else
  if (method == "ctrl") {
    return WindowOpenDisposition::NEW_BACKGROUND_TAB;
  }
#endif
  return WindowOpenDisposition::CURRENT_TAB;
}

// The shape of an activation click (see koan_glance.h): renderer-initiated
// NEW_POPUP from a trusted user gesture. window.open() popups take the
// CreateNewWindow path, not OpenURLFromTab, and context-menu actions are
// browser-initiated, so neither looks like this.
bool IsActivationClick(const content::OpenURLParams& params) {
  return params.disposition == WindowOpenDisposition::NEW_POPUP &&
         params.is_renderer_initiated && params.user_gesture &&
         params.triggering_event_info ==
             blink::mojom::TriggeringEventInfo::kFromTrustedEvent;
}

// The dimmed page behind the card. Zen fades the page to 0.3 opacity over
// the window background; the page is a native window under the overlay, so
// the window background is laid over it at 0.7 instead. Also draws the
// card's shadow.
class KoanGlanceBackdrop : public views::View {
  METADATA_HEADER(KoanGlanceBackdrop, views::View)

 public:
  KoanGlanceBackdrop() {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetCanProcessEventsWithinSubtree(false);
  }

  void SetCardBounds(const gfx::Rect& card_bounds) {
    card_bounds_ = card_bounds;
    SchedulePaint();
  }

  // The shadow belongs to the card at its full size: hidden while the card
  // grows or shrinks.
  void SetShowShadow(bool show) {
    show_shadow_ = show;
    SchedulePaint();
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* colors = GetColorProvider();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(kColorKoanGlanceBackdrop));
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()),
                          kContentCardCornerRadius, flags);

    if (!show_shadow_) {
      return;
    }
    // Only around the card: a drop shadow also fills the shape itself.
    const SkRRect card =
        SkRRect::MakeRectXY(gfx::RectToSkRect(card_bounds_),
                            kContentCardCornerRadius, kContentCardCornerRadius);
    gfx::ScopedCanvas scoped(canvas);
    canvas->sk_canvas()->clipRRect(card, SkClipOp::kDifference, true);
    cc::PaintFlags shadow;
    shadow.setAntiAlias(true);
    shadow.setColor(SK_ColorTRANSPARENT);
    shadow.setLooper(gfx::CreateShadowDrawLooper(
        {gfx::ShadowValue(kCardShadowOffset, kCardShadowBlur,
                          colors->GetColor(kColorKoanGlanceCardShadow))}));
    canvas->sk_canvas()->drawRRect(card, shadow);
  }

 private:
  gfx::Rect card_bounds_;
  bool show_shadow_ = false;
};

BEGIN_METADATA(KoanGlanceBackdrop)
END_METADATA

}  // namespace

// The overlay: the dimmed backdrop, the card holding the Glance page, and a
// column of buttons beside the card. The page is a native window positioned
// by the WebView's NativeViewHost, not part of the card's layer tree, so the
// grow and shrink animations run on that window's layer. Transforming the
// card would neither scale nor fade the page, and NativeViewHost sizes the
// page window with its ancestors' transforms.
class KoanGlanceView : public views::View {
  METADATA_HEADER(KoanGlanceView, views::View)

 public:
  KoanGlanceView(KoanGlanceController* controller,
                 content::WebContents* contents)
      : controller_(controller) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
    GetViewAccessibility().SetName(u"Glance");
    // Esc with the focus on a button (the page handles its own).
    AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));

    backdrop_ = AddChildView(std::make_unique<KoanGlanceBackdrop>());

    // On its own layer, like the backdrop and the buttons, so the page
    // window is stacked between them.
    card_ = AddChildView(std::make_unique<views::View>());
    card_->SetPaintToLayer();
    card_->layer()->SetFillsBoundsOpaquely(false);
    card_->SetLayoutManager(std::make_unique<views::FillLayout>());
    web_view_ = card_->AddChildView(
        std::make_unique<views::WebView>(contents->GetBrowserContext()));
    web_view_->SetWebContents(contents);

    buttons_ = AddChildView(std::make_unique<views::BoxLayoutView>());
    buttons_->SetPaintToLayer();
    buttons_->layer()->SetFillsBoundsOpaquely(false);
    buttons_->SetOrientation(views::BoxLayout::Orientation::kVertical);
    buttons_->SetBetweenChildSpacing(kButtonGap);
    close_ = AddButton(vector_icons::kCloseIcon, u"Close (Esc)",
                       &KoanGlanceController::Close);
    AddButton(kOpenInNewIcon, u"Expand Glance",
              &KoanGlanceController::ExpandToTab);
    AddButton(kSplitSceneIcon, u"Split Glance",
              &KoanGlanceController::OpenInSplit);
  }

  views::WebView* web_view() { return web_view_; }
  const gfx::Rect& card_bounds() const { return card_->bounds(); }

  // Whether the card grows and shrinks: only where the page is an Aura
  // window. On macOS it is an NSView composited outside the views layer
  // tree, so the overlay just appears and goes.
  bool CanAnimate() { return Page() != nullptr; }

  // Grows the card out of `rect` (in this view's coordinates). With
  // `fresh`, the backdrop and the buttons fade in too; otherwise they stay
  // (a new page replacing the card's).
  void PlayOpenAnimation(const gfx::Rect& rect, bool fresh) {
    ++animation_id_;
    closing_ = false;
    ui::LayerOwner* page = Page();
    if (!page) {
      backdrop_->layer()->SetOpacity(1.f);
      buttons_->layer()->SetOpacity(1.f);
      backdrop_->SetShowShadow(true);
      return;
    }
    backdrop_->SetShowShadow(false);
    views::AnimationBuilder builder;
    // Not when aborted: a close or another grow took over.
    builder.OnEnded(base::BindOnce(&KoanGlanceView::ShowShadow,
                                   weak_factory_.GetWeakPtr(), animation_id_));
    page->layer()->SetTransform(MapBoundsToRect(card_->bounds(), rect));
    page->layer()->SetOpacity(0.f);
    builder.Once()
        .SetDuration(kAnimationDuration)
        .SetTransform(page, gfx::Transform(), kGrowTween)
        .At(base::TimeDelta())
        .SetDuration(kPageFadeInDuration)
        .SetOpacity(page, 1.f, gfx::Tween::EASE_IN_OUT);
    if (!fresh) {
      return;
    }
    backdrop_->layer()->SetOpacity(0.f);
    buttons_->layer()->SetOpacity(0.f);
    builder.Once()
        .SetDuration(kAnimationDuration)
        .SetOpacity(backdrop_, 1.f, gfx::Tween::EASE_OUT)
        .At(base::TimeDelta())
        .SetDuration(kButtonsFadeDuration)
        .SetOpacity(buttons_, 1.f, gfx::Tween::EASE_OUT);
  }

  // Shrinks the card back into `rect` while the page fades out, and fades
  // the backdrop and the buttons out. Only where CanAnimate().
  void PlayCloseAnimation(const gfx::Rect& rect) {
    closing_ = true;
    backdrop_->SetShowShadow(false);
    views::AnimationBuilder builder;
    if (ui::LayerOwner* page = Page()) {
      builder.Once()
          .SetDuration(kAnimationDuration)
          .SetTransform(page, MapBoundsToRect(card_->bounds(), rect),
                        kShrinkTween)
          .At(base::TimeDelta())
          .SetDuration(kPageFadeOutDuration)
          .SetOpacity(page, 0.f, gfx::Tween::EASE_IN_OUT);
    }
    builder.Once()
        .SetDuration(kBackgroundRestoreDuration)
        .SetOpacity(backdrop_, 0.f, gfx::Tween::EASE_OUT)
        .At(base::TimeDelta())
        .SetDuration(kButtonsFadeDuration)
        .SetOpacity(buttons_, 0.f, gfx::Tween::EASE_OUT);
  }

  // Ends the page's animations at their full-size, opaque end state: the
  // page is about to move into a tab, whose NativeViewHost keeps the
  // transform it finds as the page's own.
  void FinishPageAnimations() {
    if (ui::LayerOwner* page = Page()) {
      page->layer()->GetAnimator()->StopAnimating();
      page->layer()->SetTransform(gfx::Transform());
      page->layer()->SetOpacity(1.f);
    }
  }

  // Zen's close button asking to confirm (zen-glance.css's
  // [waitconfirmation]): red with a white icon.
  void SetCloseConfirming(bool confirming) {
    StyleButton(
        close_, vector_icons::kCloseIcon,
        confirming ? kColorKoanGlanceButtonConfirm : kColorKoanGlanceButton,
        confirming ? kColorKoanGlanceButtonConfirmIcon
                   : kColorKoanGlanceButtonIcon);
    const std::u16string tooltip =
        confirming ? u"Press Esc again to close" : u"Close (Esc)";
    close_->SetTooltipText(tooltip);
    if (confirming) {
      GetViewAccessibility().AnnounceAlert(tooltip);
    }
  }

  // views::View:
  void Layout(PassKey) override {
    // NativeViewHost writes the page window's transform when it lays out,
    // which would snap a running grow animation; finish it first.
    if (ui::LayerOwner* page = Page()) {
      page->layer()->GetAnimator()->StopAnimatingProperty(
          ui::LayerAnimationElement::TRANSFORM);
    }
    const gfx::Rect bounds = GetLocalBounds();
    gfx::Rect card = bounds;
    card.ClampToCenteredSize(
        gfx::Size(static_cast<int>(bounds.width() * kCardWidthFraction),
                  bounds.height()));
    card_->SetBoundsRect(card);
    backdrop_->SetBoundsRect(bounds);
    backdrop_->SetCardBounds(card);
    const gfx::Size buttons_size = buttons_->GetPreferredSize();
    buttons_->SetBounds(card.right() + kButtonGap, card.y() + kButtonsTop,
                        buttons_size.width(), buttons_size.height());
    web_view_->holder()->SetNativeViewCornerRadii(
        gfx::RoundedCornersF(kContentCardCornerRadius));
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    // Only the dimmed backdrop gets here; the card and buttons take their
    // own events.
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    controller_->PostAction(&KoanGlanceController::Close);
  }

  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    controller_->OnEscape();
    return true;
  }

 private:
  void ShowShadow(int animation_id) {
    if (animation_id == animation_id_ && !closing_) {
      backdrop_->SetShowShadow(true);
    }
  }

  // The page's native window, once attached: its layer is what grows and
  // shrinks. Null on macOS (see CanAnimate()).
  ui::LayerOwner* Page() {
#if defined(USE_AURA)
    aura::Window* page = web_view_->holder()->native_view();
    return page && page->layer() ? page : nullptr;
#else
    return nullptr;
#endif
  }

  static void StyleButton(views::ImageButton* button,
                          const gfx::VectorIcon& icon,
                          ui::ColorId background,
                          ui::ColorId foreground) {
    views::SetImageFromVectorIconWithColor(
        button, icon, kButtonIconSize,
        views::IconColors(foreground, foreground, foreground));
    button->SetBackground(views::CreateRoundedRectBackground(
        background,
        views::LayoutProvider::Get()->GetCornerRadiusMetric(
            views::Emphasis::kMaximum, gfx::Size(kButtonSize, kButtonSize))));
  }

  views::ImageButton* AddButton(const gfx::VectorIcon& icon,
                                std::u16string tooltip,
                                void (KoanGlanceController::*action)()) {
    // Zen's sidebar buttons: inverted surface, accent-coloured icon.
    auto button = views::CreateVectorImageButton(
        base::BindRepeating(&KoanGlanceController::PostAction,
                            base::Unretained(controller_), action));
    StyleButton(button.get(), icon, kColorKoanGlanceButton,
                kColorKoanGlanceButtonIcon);
    button->SetTooltipText(tooltip);
    button->GetViewAccessibility().SetName(tooltip);
    button->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
    return buttons_->AddChildView(std::move(button));
  }

  const raw_ptr<KoanGlanceController> controller_;
  raw_ptr<KoanGlanceBackdrop> backdrop_ = nullptr;
  raw_ptr<views::View> card_ = nullptr;
  raw_ptr<views::WebView> web_view_ = nullptr;
  raw_ptr<views::BoxLayoutView> buttons_ = nullptr;
  raw_ptr<views::ImageButton> close_ = nullptr;
  // The latest open animation, so an earlier one's end doesn't show the
  // shadow; and whether the card is shrinking away.
  int animation_id_ = 0;
  bool closing_ = false;
  base::WeakPtrFactory<KoanGlanceView> weak_factory_{this};
};

BEGIN_METADATA(KoanGlanceView)
END_METADATA

GlanceClick MaybeOpenGlance(BrowserWindowInterface* browser,
                            content::WebContents* source,
                            const content::OpenURLParams& params,
                            WindowOpenDisposition* fallback) {
  if (!features::IsGlanceEnabled() || !browser || !IsActivationClick(params)) {
    return GlanceClick::kNone;
  }
  // Links in an extension's popup come without a source.
  KoanGlanceController* glance = GlanceControllerFor(source);
  if (!glance || !params.url.SchemeIsHTTPOrHTTPS() ||
      browser->GetTabStripModel()->GetActiveWebContents() != source) {
    *fallback = FallbackDisposition(browser->GetProfile()->GetPrefs());
    return GlanceClick::kDeclined;
  }
  glance->Open(params, source);
  return GlanceClick::kOpened;
}

void AddZenLinkContextMenuItems(content::WebContents* source,
                                const GURL& link_url,
                                ui::SimpleMenuModel* menu) {
  const tabs::TabInterface* tab =
      tabs::TabInterface::MaybeGetFromContents(source);
  if (const std::optional<size_t> split =
          menu->GetIndexOfCommandId(IDC_CONTENT_CONTEXT_OPENLINKSPLITVIEW);
      split && !(tab && tab->IsSplit())) {
    menu->SetLabel(*split, u"Split Link to New Tab");
  }
  if (link_url.SchemeIsHTTPOrHTTPS() && GlanceControllerFor(source)) {
    menu->AddItem(kGlanceLinkCommandId, u"Open Link in Glance");
  }
}

void OpenLinkInGlance(content::WebContents* source,
                      const content::ContextMenuParams& params) {
  KoanGlanceController* glance = GlanceControllerFor(source);
  if (!glance) {
    return;
  }
  // Like upstream's "Open link in new tab": the frame's referrer (under its
  // policy) and origin.
  content::OpenURLParams open_params(
      params.link_url,
      content::Referrer::SanitizeForRequest(
          params.link_url, content::Referrer(params.frame_url.GetAsReferrer(),
                                             params.referrer_policy)),
      WindowOpenDisposition::CURRENT_TAB, ui::PAGE_TRANSITION_LINK,
      /*is_renderer_initiated=*/false);
  open_params.initiator_origin = params.frame_origin;
  glance->Open(open_params, source);
}

KoanGlanceController::InputTracker::InputTracker(content::WebContents* contents)
    : content::WebContentsObserver(contents) {}

void KoanGlanceController::InputTracker::DidGetUserInteraction(
    const blink::WebInputEvent& event) {
  if (event.GetType() == blink::WebInputEvent::Type::kMouseDown) {
    // Any button: the activation click, or the right click that opened the
    // context menu. A press in a cross-site iframe comes in the iframe's
    // coordinates, so the card grows out of a point off the click there.
    const gfx::PointF position =
        static_cast<const blink::WebMouseEvent&>(event).PositionInWidget();
    press_ = Press{web_contents()->GetContainerBounds().origin() +
                       gfx::ToFlooredPoint(position).OffsetFromOrigin(),
                   base::TimeTicks::Now()};
  } else if (blink::WebInputEvent::IsKeyboardEventType(event.GetType())) {
    press_.reset();
  }
}

void KoanGlanceController::InputTracker::PrimaryPageChanged(
    content::Page& page) {
  // The old page's last focus change can be dropped once the new one has
  // committed.
  editing_ = false;
}

void KoanGlanceController::InputTracker::OnFocusChangedInPage(
    const content::FocusedNodeDetails& details) {
  editing_ = details.editable_level != content::EditableLevel::kNotEditable;
}

KoanGlanceController::KoanGlanceController(BrowserView* browser_view)
    : browser_view_(browser_view) {
  browser_view_->browser()->GetTabStripModel()->AddObserver(this);
  TrackActiveTab();
}

KoanGlanceController::~KoanGlanceController() = default;

void KoanGlanceController::TrackActiveTab() {
  content::WebContents* active =
      browser_view_->browser()->GetTabStripModel()->GetActiveWebContents();
  if (!active) {
    tab_input_.reset();
  } else if (!tab_input_ || tab_input_->web_contents() != active) {
    tab_input_ = std::make_unique<InputTracker>(active);
  }
}

void KoanGlanceController::Open(const content::OpenURLParams& params,
                                content::WebContents* source) {
  // "Open Link in Glance" in the Glance page: it belongs to the tab under it
  // (the page may be going away with a closing card).
  if (source == contents_.get()) {
    source = source_;
  }
  if (closing_) {
    TearDown();
  }
  if (is_open()) {
    // One card at a time: it shows the new link instead, growing out of the
    // click again.
    EndConfirm();
    contents_->GetController().LoadURLWithParams(
        content::NavigationController::LoadURLParams(params));
    grow_rect_ = GrowRect();
    view_->PlayOpenAnimation(grow_rect_, /*fresh=*/false);
    return;
  }
  source_ = source;

  content::WebContents::CreateParams create_params(browser_view_->GetProfile());
  contents_ = content::WebContents::Create(create_params);
  contents_->SetDelegate(this);
  contents_->SetUserData(kGlancePageKey, std::make_unique<GlancePage>(this));
  // Popups without a user gesture are blocked, as in a tab.
  blocked_content::PopupBlockerTabHelper::CreateForWebContents(contents_.get());
  contents_->GetController().LoadURLWithParams(
      content::NavigationController::LoadURLParams(params));
  glance_input_ = std::make_unique<InputTracker>(contents_.get());

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
  widget_->LayoutRootViewIfNecessary();
  grow_rect_ = GrowRect();
  view_->PlayOpenAnimation(grow_rect_, /*fresh=*/true);
  view_->web_view()->RequestFocus();
}

void KoanGlanceController::PostAction(void (KoanGlanceController::*action)()) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(action, weak_factory_.GetWeakPtr()));
}

void KoanGlanceController::Close() {
  if (!is_open() || closing_ || awaiting_beforeunload_) {
    return;
  }
  // Like Zen's permitUnload(): the page may ask to stay (BeforeUnloadFired).
  if (contents_->NeedToFireBeforeUnload()) {
    awaiting_beforeunload_ = true;
    contents_->DispatchBeforeUnload(/*auto_cancel=*/false);
    return;
  }
  CloseNow();
}

void KoanGlanceController::CloseNow() {
  EndConfirm();
  closing_ = true;
  if (source_) {
    source_->Focus();
  }
  if (!view_->CanAnimate()) {
    TearDown();
    return;
  }
  view_->PlayCloseAnimation(grow_rect_);
  close_timer_.Start(FROM_HERE, kAnimationDuration,
                     base::BindOnce(&KoanGlanceController::TearDown,
                                    weak_factory_.GetWeakPtr()));
}

void KoanGlanceController::OnEscape() {
  if (!is_open() || closing_) {
    return;
  }
  // Zen asks first while something in the page has focus; Koan only for
  // editable fields, where Esc often means "dismiss the suggestions".
  if (glance_input_ && glance_input_->editing() &&
      !confirm_timer_.IsRunning()) {
    view_->SetCloseConfirming(true);
    confirm_timer_.Start(FROM_HERE, kConfirmDuration,
                         base::BindOnce(&KoanGlanceController::EndConfirm,
                                        weak_factory_.GetWeakPtr()));
    return;
  }
  // The key event is still being dispatched; close once that unwinds.
  PostAction(&KoanGlanceController::Close);
}

void KoanGlanceController::EndConfirm() {
  confirm_timer_.Stop();
  if (view_) {
    view_->SetCloseConfirming(false);
  }
}

void KoanGlanceController::ReleaseContents() {
  // The page's dialogs (and the dialog manager's memory of them, keyed by
  // the WebContents) end with the card; without a delegate, the WebContents
  // wouldn't reset them itself.
  javascript_dialogs::AppModalDialogManager::GetInstance()->CancelDialogs(
      contents_.get(), /*reset_state=*/true);
  contents_->SetDelegate(nullptr);
}

void KoanGlanceController::TearDown() {
  close_timer_.Stop();
  confirm_timer_.Stop();
  if (!is_open()) {
    return;
  }
  DestroyWidget();
  ReleaseContents();
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, std::move(contents_));
  glance_input_.reset();
  source_ = nullptr;
  closing_ = false;
  awaiting_beforeunload_ = false;
}

std::unique_ptr<content::WebContents> KoanGlanceController::TakeContents() {
  close_timer_.Stop();
  confirm_timer_.Stop();
  view_->FinishPageAnimations();
  DestroyWidget();
  ReleaseContents();
  contents_->RemoveUserData(kGlancePageKey);
  // The tab gets its own (TabFeatures takes an existing one as a sign the
  // contents were a tab before).
  contents_->RemoveUserData(
      blocked_content::PopupBlockerTabHelper::UserDataKey());
  glance_input_.reset();
  source_ = nullptr;
  awaiting_beforeunload_ = false;
  return std::move(contents_);
}

void KoanGlanceController::ExpandToTab() {
  // Not while beforeunload runs: its answer would close the tab.
  if (!is_open() || closing_ || awaiting_beforeunload_) {
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
  if (!is_open() || closing_ || awaiting_beforeunload_) {
    return;
  }
  TabStripModel* model = browser_view_->browser()->GetTabStripModel();
  const int source_index = model->GetIndexOfWebContents(source_);
  if (source_index == TabStripModel::kNoTab ||
      model->GetTabAtIndex(source_index)->IsSplit()) {
    // A third panel for the split: over Chromium's limit. Zen shows its
    // toast; the page opens as a tab next to the split instead.
    ShowSplitLimitToast(browser_view_);
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
  // A shrinking card keeps its bounds: a relayout would snap the page back.
  if (is_open() && !closing_) {
    LayoutWidget();
  }
}

void KoanGlanceController::OnBrowserViewDestroying() {
  browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
  close_timer_.Stop();
  confirm_timer_.Stop();
  DestroyWidget();
  if (contents_) {
    ReleaseContents();
    contents_.reset();
  }
  glance_input_.reset();
  tab_input_.reset();
  source_ = nullptr;
  closing_ = false;
  awaiting_beforeunload_ = false;
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
  // Over the content card, not its gutter: at the top of the window that
  // is where the frame paints the caption buttons, above everything else.
  MultiContentsView* contents_view = browser_view_->multi_contents_view();
  gfx::Rect bounds = contents_view->GetLocalBounds();
  if (UseContentCard(browser_view_)) {
    bounds.Inset(ContentCardInsets(browser_view_));
  }
  widget_->SetBounds(contents_view->ConvertRectToWidget(bounds));
}

gfx::Rect KoanGlanceController::GrowRect() const {
  const gfx::Rect card = view_->card_bounds();
  // The latest press, in the tab or in the Glance page.
  std::optional<InputTracker::Press> press;
  for (const InputTracker* tracker : {tab_input_.get(), glance_input_.get()}) {
    if (tracker && tracker->press() &&
        (!press || tracker->press()->time > press->time)) {
      press = tracker->press();
    }
  }
  gfx::Point center = card.CenterPoint();
  if (press) {
    center = press->point -
             widget_->GetWindowBoundsInScreen().origin().OffsetFromOrigin();
  }
  gfx::Rect rect(gfx::ScaleToRoundedSize(card.size(), kGrowFraction));
  rect.set_origin(center - gfx::Vector2d(rect.width() / 2, rect.height() / 2));
  rect.AdjustToFit(card);
  return rect;
}

content::WebContents* KoanGlanceController::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>
        navigation_handle_callback) {
  // An activation click in the Glance page: the card shows the link. One
  // Glance declines gets its modifier's usual disposition, "in place"
  // meaning in the card.
  if (IsActivationClick(params) && !closing_ && source == contents_.get()) {
    if (IsGlanceOn(source) && params.url.SchemeIsHTTPOrHTTPS()) {
      Open(params, source_);
      return contents_.get();
    }
    content::OpenURLParams fallback(params);
    fallback.disposition = FallbackDisposition(PrefsFor(source));
    return OpenURLFromTab(source, fallback,
                          std::move(navigation_handle_callback));
  }
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
  // window.close() arrives mid renderer message; close afterwards.
  if (source == contents_.get()) {
    PostAction(&KoanGlanceController::Close);
  }
}

bool KoanGlanceController::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  if (event.GetType() == blink::WebInputEvent::Type::kRawKeyDown &&
      event.windows_key_code == ui::VKEY_ESCAPE) {
    OnEscape();
    return true;
  }
  // Everything else (Ctrl+T, Ctrl+W, ...) behaves as in a tab.
  return browser_view_->HandleKeyboardEvent(event);
}

void KoanGlanceController::BeforeUnloadFired(content::WebContents* source,
                                             bool proceed,
                                             bool* proceed_to_fire_unload) {
  *proceed_to_fire_unload = proceed;
  if (source != contents_.get() || !awaiting_beforeunload_) {
    return;
  }
  awaiting_beforeunload_ = false;
  if (proceed) {
    CloseNow();
  }
}

content::JavaScriptDialogManager*
KoanGlanceController::GetJavaScriptDialogManager(content::WebContents* source) {
  // The page's dialogs (incl. "Leave site?") as for other WebContents that
  // aren't tabs, e.g. DevTools'.
  return javascript_dialogs::AppModalDialogManager::GetInstance();
}

void KoanGlanceController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  TrackActiveTab();
  // Glance belongs to the tab it was opened from: drop it at once when
  // another tab becomes active or the source tab leaves this tab strip.
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
    source_ = nullptr;
  }
  if (source_removed || selection.active_tab_changed()) {
    TearDown();
  }
}

}  // namespace koan
