// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/strings/strcat.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/koan_shortcut_table.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_compact_mode.h"
#include "koan/browser/ui/views/koan_library.h"
#include "koan/browser/ui/views/koan_library_view.h"
#include "koan/browser/ui/views/koan_shortcuts.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/cubic_bezier.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"

namespace koan {

namespace {

// Zen's spring for opening and closing (stiffness 720, damping 47,
// mass 1.2): slightly under-damped, settled after about 400ms.
constexpr double kStiffness = 720;
constexpr double kDamping = 47;
constexpr double kMass = 1.2;
constexpr base::TimeDelta kSlideDuration = base::Milliseconds(450);
// Zen: transition: width 0.3s cubic-bezier(.36, .06, 0, .94).
constexpr base::TimeDelta kResizeDuration = base::Milliseconds(300);
// Zen unmounts the closed library's sections after 30s.
constexpr base::TimeDelta kCleanupDelay = base::Seconds(30);
// The library never covers more than 95% of the window (95vw).
constexpr float kMaxWidthFraction = 0.95f;

// Where a spring released at 0 heads to 1 after `seconds`.
double Spring(double seconds) {
  const double omega = std::sqrt(kStiffness / kMass);
  const double zeta = kDamping / (2 * std::sqrt(kStiffness * kMass));
  const double omega_d = omega * std::sqrt(1 - zeta * zeta);
  const double decay = std::exp(-zeta * omega * seconds);
  return 1 - decay * (std::cos(omega_d * seconds) +
                      zeta * omega / omega_d * std::sin(omega_d * seconds));
}

const char* SectionPrefValue(LibrarySection section) {
  switch (section) {
    case LibrarySection::kDownloads:
      return "downloads";
    case LibrarySection::kSpaces:
      return "spaces";
    case LibrarySection::kHistory:
      return "history";
  }
  return "history";
}

LibrarySection SectionFromPref(const std::string& value) {
  if (value == "downloads") {
    return LibrarySection::kDownloads;
  }
  if (value == "spaces") {
    return LibrarySection::kSpaces;
  }
  return LibrarySection::kHistory;
}

// The Library button at the bottom of the sidebar.
class KoanLibraryButton : public views::ImageButton {
  METADATA_HEADER(KoanLibraryButton, views::ImageButton)

 public:
  KoanLibraryButton(KoanLibraryController* library,
                    std::optional<ui::Accelerator> accelerator)
      : views::ImageButton(
            base::BindRepeating(&KoanLibraryController::Toggle,
                                base::Unretained(library),
                                std::optional<LibrarySection>())) {
    SetImageModel(STATE_NORMAL, ui::ImageModel::FromVectorIcon(
                                    kLibraryIcon, kColorKoanLibraryForeground,
                                    library::kLibraryButtonIconSize));
    SetImageHorizontalAlignment(ALIGN_CENTER);
    SetImageVerticalAlignment(ALIGN_MIDDLE);
    SetPreferredSize(
        gfx::Size(library::kLibraryButtonSize, library::kLibraryButtonSize));
    SetBackground(library::CreateStateBackground(
        std::nullopt, kColorKoanLibraryPillHovered, library::kSmallRadius));
    // The key the user has for it, e.g. "Library (Alt+⇧+L)".
    SetTooltipText(
        accelerator ? base::StrCat({u"Library (",
                                    shortcuts::DisplayText(*accelerator), u")"})
                    : u"Library");
    GetViewAccessibility().SetName(u"Library");
  }
};

BEGIN_METADATA(KoanLibraryButton)
END_METADATA

}  // namespace

KoanLibraryController::KoanLibraryController(BrowserView* browser_view)
    : browser_view_(browser_view),
      section_(
          SectionFromPref(browser_view->GetProfile()->GetPrefs()->GetString(
              prefs::kLibraryLastSection))) {
  slide_.SetDuration(kSlideDuration);
  resize_.SetDuration(kResizeDuration);
  if (IsAvailable()) {
    browser_view_->browser()->GetTabStripModel()->AddObserver(this);
  }
}

KoanLibraryController::~KoanLibraryController() = default;

bool KoanLibraryController::IsAvailable() const {
  // Normal windows of a regular profile: an incognito window has no history
  // or downloads of its own to show.
  return features::IsLibraryEnabled() && browser_view_->GetIsNormalType() &&
         !browser_view_->GetProfile()->IsOffTheRecord();
}

void KoanLibraryController::Toggle(std::optional<LibrarySection> section) {
  if (!IsAvailable()) {
    return;
  }
  if (!section) {
    open_ ? Close() : Open();
    return;
  }
  if (open_ && section_ == *section) {
    Close();
    return;
  }
  ShowSection(*section);
  Open();
}

void KoanLibraryController::Open() {
  if (!IsAvailable()) {
    return;
  }
  if (open_) {
    view_->OnOpened();
    return;
  }
  open_ = true;
  cleanup_timer_.Stop();
  if (!widget_) {
    CreateWidget();
  }
  view_->ShowSection(section_);
  width_ = GetTargetWidth();
  resize_.Stop();
  LayoutWidget();
  widget_->Show();
  SetEscapeRegistered(true);
  AnimateTo(1);
  view_->OnOpened();
}

void KoanLibraryController::Close() {
  if (!open_) {
    return;
  }
  open_ = false;
  SetEscapeRegistered(false);
  // Give the keyboard back to the page if the library had it.
  views::FocusManager* focus_manager = browser_view_->GetFocusManager();
  if (focus_manager && focus_manager->GetFocusedView() &&
      focus_manager->GetFocusedView()->GetWidget() == widget_.get()) {
    if (content::WebContents* contents = browser_view_->browser()
                                             ->GetTabStripModel()
                                             ->GetActiveWebContents()) {
      contents->Focus();
    }
  }
  AnimateTo(0);
}

void KoanLibraryController::ShowSection(LibrarySection section) {
  section_ = section;
  browser_view_->GetProfile()->GetPrefs()->SetString(prefs::kLibraryLastSection,
                                                     SectionPrefValue(section));
  if (view_) {
    view_->ShowSection(section);
  }
}

void KoanLibraryController::KeepOpenWhile(base::OnceClosure open_tab) {
  keeping_open_ = true;
  std::move(open_tab).Run();
  keeping_open_ = false;
}

void KoanLibraryController::OnPreferredWidthChanged() {
  if (!widget_) {
    return;
  }
  const int target = GetTargetWidth();
  if (target == width_to_ && resize_.is_animating()) {
    return;
  }
  if (!open_ || slide_.is_animating() || !widget_->IsVisible()) {
    resize_.Stop();
    width_ = target;
    LayoutWidget();
    return;
  }
  width_from_ = width_;
  width_to_ = target;
  resize_.Stop();
  resize_.Start();
}

void KoanLibraryController::OnBrowserViewLayout() {
  if (widget_ && widget_->IsVisible()) {
    if (!resize_.is_animating()) {
      width_ = GetTargetWidth();
    }
    LayoutWidget();
  }
}

void KoanLibraryController::OnBrowserViewDestroying() {
  browser_view_->browser()->GetTabStripModel()->RemoveObserver(this);
  SetEscapeRegistered(false);
  slide_.Stop();
  resize_.Stop();
  DestroyWidget();
}

void KoanLibraryController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // Zen closes the library when a tab opens, unless the library opened it
  // in the background itself.
  if (open_ && !keeping_open_ &&
      change.type() == TabStripModelChange::kInserted) {
    Close();
  }
}

void KoanLibraryController::AnimationProgressed(
    const gfx::Animation* animation) {
  if (animation == &slide_) {
    const double t = slide_.GetCurrentValue() * kSlideDuration.InSecondsF();
    SetProgress(from_ + (target_ - from_) * Spring(t));
  } else if (animation == &resize_) {
    const double eased =
        gfx::CubicBezier(0.36, 0.06, 0, 0.94).Solve(resize_.GetCurrentValue());
    width_ = std::lround(width_from_ + (width_to_ - width_from_) * eased);
    LayoutWidget();
  }
}

void KoanLibraryController::AnimationEnded(const gfx::Animation* animation) {
  if (animation == &slide_) {
    Finish();
  } else if (animation == &resize_) {
    width_ = width_to_;
    LayoutWidget();
  }
}

bool KoanLibraryController::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (!open_) {
    return false;
  }
  // A section may use Esc itself (to stop renaming a space).
  if (view_ && view_->HandleEscape()) {
    return true;
  }
  Close();
  return true;
}

bool KoanLibraryController::CanHandleAccelerators() const {
  return open_;
}

void KoanLibraryController::CreateWidget() {
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  params.parent = browser_view_->GetWidget()->GetNativeView();
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.name = "KoanLibrary";
  widget_ = std::make_unique<views::Widget>();
  widget_->Init(std::move(params));
  view_ = widget_->SetContentsView(std::make_unique<KoanLibraryView>(this));
  // The panel slides in from the window's edge, not from beyond it.
  widget_->GetLayer()->SetMasksToBounds(true);
}

void KoanLibraryController::DestroyWidget() {
  cleanup_timer_.Stop();
  view_ = nullptr;
  if (widget_) {
    widget_->CloseNow();
    widget_.reset();
  }
  progress_ = 0;
}

void KoanLibraryController::AnimateTo(double target) {
  slide_.Stop();
  from_ = progress_;
  target_ = target;
  slide_.Start();
}

void KoanLibraryController::SetProgress(double progress) {
  progress_ = progress;
  if (!view_) {
    return;
  }
  // It slides in from the sidebar's side of the window.
  gfx::Transform transform;
  transform.Translate((on_right_ ? 1 : -1) * (1 - progress) * width_, 0);
  view_->layer()->SetTransform(transform);
}

void KoanLibraryController::Finish() {
  SetProgress(target_);
  if (target_ == 0 && widget_) {
    widget_->Hide();
    cleanup_timer_.Start(FROM_HERE, kCleanupDelay,
                         base::BindOnce(&KoanLibraryController::DestroyWidget,
                                        weak_factory_.GetWeakPtr()));
  }
}

gfx::Rect KoanLibraryController::GetArea() const {
  views::View* contents = browser_view_->contents_container();
  gfx::Rect area = contents->ConvertRectToWidget(contents->GetLocalBounds());
  // With the sidebar shown, from its top (in single toolbar mode it starts
  // above the page's card).
  views::View* sidebar = browser_view_->tab_strip_view();
  if (sidebar && sidebar->GetVisible() && sidebar->width() > 0 &&
      sidebar->GetClassName() == "VerticalTabStripRegionView") {
    area.Union(sidebar->ConvertRectToWidget(sidebar->GetLocalBounds()));
  }
  const gfx::Rect window =
      browser_view_->ConvertRectToWidget(browser_view_->GetLocalBounds());
  area.SetHorizontalBounds(window.x(), window.right());
  return area;
}

int KoanLibraryController::GetTargetWidth() const {
  const int preferred = view_ ? view_->GetPreferredWidth() : library::kMinWidth;
  const int max = std::lround(GetArea().width() * kMaxWidthFraction);
  return std::max(std::min(library::kMinWidth, max), std::min(preferred, max));
}

void KoanLibraryController::LayoutWidget() {
  if (!widget_) {
    return;
  }
  on_right_ = IsSidebarOnRight(browser_view_);
  view_->SetOnRight(on_right_);
  gfx::Rect bounds = GetArea();
  if (on_right_) {
    bounds.set_x(bounds.right() - width_);
  }
  bounds.set_width(width_);
  widget_->SetBounds(bounds);
  SetProgress(progress_);
}

void KoanLibraryController::SetEscapeRegistered(bool registered) {
  const ui::Accelerator escape(ui::VKEY_ESCAPE, ui::EF_NONE);
  if (registered && !escape_focus_manager_) {
    escape_focus_manager_ = browser_view_->GetFocusManager();
    if (escape_focus_manager_) {
      escape_focus_manager_->RegisterAccelerator(
          escape, ui::AcceleratorManager::kNormalPriority, this);
    }
  } else if (!registered && escape_focus_manager_) {
    escape_focus_manager_->UnregisterAccelerator(escape, this);
    escape_focus_manager_ = nullptr;
  }
}

std::unique_ptr<views::View> CreateLibraryButton(BrowserView* browser_view) {
  KoanBrowserViewController* controller =
      KoanBrowserViewController::From(browser_view);
  if (!controller || !controller->library()->IsAvailable()) {
    return nullptr;
  }
  return std::make_unique<KoanLibraryButton>(
      controller->library(),
      controller->shortcuts()->GetAccelerator(shortcuts::Action::kLibrary));
}

}  // namespace koan
