// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_loading_indicator.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_content_card.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rrect_f.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// Zen's offset where the page has no gap above it, or a toolbar: just inside
// the page's top edge.
constexpr float kInsidePageOffset = 4;

}  // namespace

// Draws one KoanLoadingBar frame about its middle.
class KoanLoadingBarView : public views::View {
  METADATA_HEADER(KoanLoadingBarView, views::View)

 public:
  KoanLoadingBarView() = default;

  void SetFrame(const KoanLoadingBar::Frame& frame) {
    frame_ = frame;
    SchedulePaint();
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    if (frame_.bar.IsEmpty() || frame_.opacity <= 0) {
      return;
    }
    const ui::ColorProvider* colors = GetColorProvider();
    const gfx::Vector2dF anchor =
        gfx::RectF(GetLocalBounds()).CenterPoint() - gfx::PointF();
    gfx::RectF bar = frame_.bar;
    bar.Offset(anchor);
    // border-radius: 100px, i.e. a pill.
    const float radius = std::min(bar.width(), bar.height()) / 2;
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors->GetColor(frame_.long_load ? kColorKoanLoadingBarTrack
                                                     : kColorKoanLoadingBar));
    flags.setAlphaf(flags.getAlphaf() * frame_.opacity);
    canvas->DrawRoundRect(bar, radius, flags);
    if (frame_.sweep.IsEmpty()) {
      return;
    }
    // The sweep (the track's ::before) inside the track, clipped to it.
    gfx::RectF sweep = frame_.sweep;
    sweep.Offset(anchor);
    gfx::ScopedCanvas scoped(canvas);
    canvas->ClipPath(
        SkPath::RRect(static_cast<SkRRect>(gfx::RRectF(bar, radius))),
        /*do_anti_alias=*/true);
    flags.setColor(colors->GetColor(kColorKoanLoadingBar));
    flags.setAlphaf(flags.getAlphaf() * frame_.opacity);
    canvas->DrawRoundRect(sweep, radius, flags);
  }

 private:
  KoanLoadingBar::Frame frame_;
};

BEGIN_METADATA(KoanLoadingBarView)
END_METADATA

KoanLoadingIndicator::KoanLoadingIndicator(BrowserView* browser_view)
    : browser_view_(browser_view) {
  // A long load can take minutes: the frames run as long as it does.
  set_check_active_duration(false);
  browser_view_->browser()->GetTabStripModel()->AddObserver(this);
  pref_change_registrar_.Init(
      browser_view_->GetProfile()->GetOriginalProfile()->GetPrefs());
  pref_change_registrar_.Add(prefs::kLoadingIndicator,
                             base::BindRepeating(&KoanLoadingIndicator::Update,
                                                 base::Unretained(this)));
}

KoanLoadingIndicator::~KoanLoadingIndicator() {
  StopFrames();
}

void KoanLoadingIndicator::OnAddedToWidget() {
  // The page's own fullscreen hides the bar (Zen's [inDOMFullscreen]).
  if (!fullscreen_subscription_) {
    if (auto* manager =
            ExclusiveAccessManager::From(browser_view_->browser())) {
      fullscreen_subscription_ =
          manager->fullscreen_controller()->RegisterOnFullscreenStateChanged(
              base::BindRepeating(&KoanLoadingIndicator::Update,
                                  base::Unretained(this)));
    }
  }
  Update();
}

void KoanLoadingIndicator::OnBrowserViewDestroying() {
  destroying_ = true;
  long_load_timer_.Stop();
  fullscreen_subscription_ = {};
  pref_change_registrar_.RemoveAll();
  browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
  Observe(nullptr);
  bar_.Reset();
  CloseWidget();
}

void KoanLoadingIndicator::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // A new selected tab, or its contents replaced (a discarded tab coming
  // back).
  Update();
}

void KoanLoadingIndicator::DidStartLoading() {
  Update();
}

void KoanLoadingIndicator::DidStopLoading() {
  Update();
}

void KoanLoadingIndicator::OnAnimationStep(base::TimeTicks timestamp) {
  Step(timestamp);
}

void KoanLoadingIndicator::OnCompositingShuttingDown(
    ui::Compositor* compositor) {
  StopFrames();
}

void KoanLoadingIndicator::Update() {
  // Normal windows only: popups and apps have no Zen chrome.
  if (destroying_ || !browser_view_->GetIsNormalType()) {
    return;
  }
  content::WebContents* contents =
      browser_view_->browser()->GetTabStripModel()->GetActiveWebContents();
  if (web_contents() != contents) {
    Observe(contents);
  }
  ExclusiveAccessManager* manager =
      ExclusiveAccessManager::From(browser_view_->browser());
  const bool page_fullscreen =
      manager && manager->fullscreen_controller()->IsTabFullscreen();
  const bool enabled =
      pref_change_registrar_.prefs()->GetBoolean(prefs::kLoadingIndicator);
  const base::TimeTicks now = base::TimeTicks::Now();
  if (!enabled || page_fullscreen) {
    // Gone at once, like Zen's display: none.
    long_load_timer_.Stop();
    bar_.Reset();
    Step(now);
    return;
  }
  // Zen's "busy" tab: loading another document (what Chromium's tab
  // spinner shows), not a same-document navigation.
  const bool loading = contents && contents->ShouldShowLoadingUI();
  if (loading && !bar_.IsShowing()) {
    bar_.set_reduced_motion(!gfx::Animation::ShouldRenderRichAnimation());
    bar_.Show(now);
    long_load_timer_.Start(FROM_HERE, KoanLoadingBar::kLongLoadDelay, this,
                           &KoanLoadingIndicator::OnLongLoad);
  } else if (!loading && bar_.IsShowing()) {
    long_load_timer_.Stop();
    bar_.Hide(now);
  }
  if (bar_.IsDone(now)) {
    Step(now);
    return;
  }
  if (!widget_) {
    views::Widget* browser_widget = browser_view_->GetWidget();
    if (!browser_widget) {
      return;
    }
    views::Widget::InitParams params(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_CONTROL);
    params.parent = browser_widget->GetNativeView();
    params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
    // Zen's pointer-events: none.
    params.accept_events = false;
    params.activatable = views::Widget::InitParams::Activatable::kNo;
    params.name = "KoanLoadingIndicator";
    widget_ = std::make_unique<views::Widget>();
    widget_->Init(std::move(params));
    view_ = widget_->SetContentsView(std::make_unique<KoanLoadingBarView>());
    view_->SetPaintToLayer();
    view_->layer()->SetFillsBoundsOpaquely(false);
  }
  OnBrowserViewLayout();
  if (!widget_->IsVisible()) {
    widget_->ShowInactive();
  }
  StartFrames();
  Step(now);
}

void KoanLoadingIndicator::OnLongLoad() {
  bar_.StartLongLoad(base::TimeTicks::Now());
}

void KoanLoadingIndicator::OnBrowserViewLayout() {
  MultiContentsView* contents_view = browser_view_->multi_contents_view();
  ContentsWebView* page =
      contents_view ? contents_view->GetActiveContentsView() : nullptr;
  if (!widget_ || !page || !page->GetWidget()) {
    return;
  }
  const gfx::Rect page_bounds =
      page->ConvertRectToWidget(page->GetLocalBounds());
  // Zen: centred in the gap above the page (top: -separation / 2), or just
  // inside the page without a gap (zen-no-padding) or with Chromium's
  // toolbar above it (not zen-single-toolbar).
  const int gap = UseContentCard(browser_view_)
                      ? ContentCardInsets(browser_view_).top()
                      : 0;
  const bool single_toolbar =
      pref_change_registrar_.prefs()->GetBoolean(prefs::kSingleToolbarEnabled);
  // (Not over the bookmarks bar docked in that gap, Zen's top bar.)
  const float anchor_y =
      single_toolbar && gap > 0 && !GetDockedBookmarksBarHeight(browser_view_)
          ? page_bounds.y() - gap / 2.f
          : page_bounds.y() + kInsidePageOffset;
  const int width = std::ceil(KoanLoadingBar::kMaxWidth) + 2;
  const int height = std::ceil(KoanLoadingBar::kHeight) + 2;
  widget_->SetBounds(gfx::Rect(page_bounds.CenterPoint().x() - width / 2,
                               std::lround(anchor_y - height / 2.f), width,
                               height));
}

void KoanLoadingIndicator::Step(base::TimeTicks now) {
  if (!widget_) {
    return;
  }
  view_->SetFrame(bar_.FrameAt(now));
  if (bar_.IsDone(now)) {
    // Kept for the next load; never closed from inside its compositor's
    // frame.
    StopFrames();
    widget_->Hide();
  }
}

void KoanLoadingIndicator::StartFrames() {
  ui::Compositor* compositor = widget_ ? widget_->GetCompositor() : nullptr;
  if (compositor == compositor_) {
    return;
  }
  StopFrames();
  if (compositor) {
    compositor_ = compositor;
    compositor_->AddAnimationObserver(this);
  }
}

void KoanLoadingIndicator::StopFrames() {
  if (compositor_) {
    compositor_->RemoveAnimationObserver(this);
    compositor_ = nullptr;
  }
}

void KoanLoadingIndicator::CloseWidget() {
  StopFrames();
  view_ = nullptr;
  if (widget_) {
    widget_->CloseNow();
    widget_.reset();
  }
}

}  // namespace koan
