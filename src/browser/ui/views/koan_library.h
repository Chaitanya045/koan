// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenLibrary.mjs
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "koan/browser/ui/views/koan_library_ui.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"

class BrowserView;

namespace gfx {
class AnimationContainer;
}  // namespace gfx

namespace ui {
class EventHandler;
}  // namespace ui

namespace views {
class FocusManager;
class View;
class ViewTracker;
class Widget;
}  // namespace views

namespace koan {

class KoanLibraryView;
struct LibraryMotion;

// Zen's Library for one window (zen/library/ZenLibrary.mjs): a full-height
// panel that slides in over the sidebar, with section tabs (Downloads,
// Spaces, History) down its side. Opened with Alt+Shift+L, Ctrl+H /
// Ctrl+Shift+H (History) and Ctrl+Shift+Y (Downloads), or the Library button
// at the bottom of the sidebar; closed by the same keys, Esc, the back
// button, or opening a tab.
//
// Like Glance, the panel is a child widget, so it sits above the page's
// native window and gets the pointer everywhere over it. It is kept for a
// while after closing (Zen unmounts its sections after 30s) and then freed.
class KoanLibraryController : public TabStripModelObserver,
                              public gfx::AnimationDelegate,
                              public ui::AcceleratorTarget {
 public:
  explicit KoanLibraryController(BrowserView* browser_view);
  KoanLibraryController(const KoanLibraryController&) = delete;
  KoanLibraryController& operator=(const KoanLibraryController&) = delete;
  ~KoanLibraryController() override;

  // Whether this window has a Library (normal browser windows).
  bool IsAvailable() const;
  // Whether the library is open or opening.
  bool is_open() const { return open_; }
  LibrarySection section() const { return section_; }
  BrowserView* browser_view() const { return browser_view_; }

  // Zen's ZenLibrary.toggle(): without a section, opens or closes. With one,
  // opens on that section, switches to it if the library shows another, or
  // closes if it already shows it.
  void Toggle(std::optional<LibrarySection> section = std::nullopt);
  void Open();
  void Close();
  void ShowSection(LibrarySection section);

  // Zen's Library swipe (ZenLibrary startSwipe(), swipeProgress(),
  // stopSwipe()), driven by the two-finger swipe over the sidebar or the panel
  // (KoanWorkspaceController, koan_swipe_edge_actions.h). StartSwipe() shows
  // the panel where it is and returns its open progress, SwipeTo() moves it to
  // `progress` (0 closed, 1 open, a little past either), EndSwipe() opens or
  // closes it on the spring from there. Opening or closing it any other way
  // ends the swipe's hold on it.
  double StartSwipe();
  void SwipeTo(double progress);
  void EndSwipe(bool open);
  bool is_swiping() const { return swiping_; }
  // Whether the panel is on the screen's right (the sidebar's side), also
  // while it doesn't exist.
  bool IsOnScreenRight() const;

  // Runs `open_tab`, which opens a tab from inside the library, without that
  // tab closing the library (a page opened in the background).
  void KeepOpenWhile(base::OnceClosure open_tab);

  // Keeps the panel and its sections from being freed after it closes for
  // as long as the returned runner lives: a confirmation the user can still
  // accept after the library closed behind it.
  base::ScopedClosureRunner KeepAlive();

  // A section changed the width it wants (Spaces grows with its cards).
  void OnPreferredWidthChanged();

  // Called by KoanBrowserViewController.
  void OnBrowserViewLayout();
  void OnBrowserViewDestroying();

  // The panel's view, while it exists (for tests and the sections).
  KoanLibraryView* view() const { return view_; }

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  // ui::AcceleratorTarget:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;

  // Its widget while it shows, for the window's colours
  // (KoanThemeController::AdjustChildColorKey()).
  const views::Widget* widget() const { return widget_.get(); }

 private:
  void CreateWidget();
  void DestroyWidget();
  // Slides the panel towards `target` (0 closed, 1 open) on Zen's spring.
  void AnimateTo(double target);
  // Steps the spring with the window's compositor frames.
  void UseCompositorFrames();
  // Moves the panel, and the sidebar under it, to `progress`
  // (GetLibraryMotion(): the sidebar collapses first in compact mode, else
  // grows into the panel).
  void SetProgress(double progress);
  // The sidebar (the vertical tab strip's region) while it is shown.
  views::View* GetSidebar() const;
  void MoveSidebar(views::View* sidebar, const LibraryMotion& motion);
  // The Library has closed: the sidebar is back as it was.
  void RestoreSidebar();
  void Finish();
  void ReleaseKeepAlive();
  void ScheduleCleanup();
  // The space the library covers: the window below any toolbar row, from
  // the sidebar's edge.
  gfx::Rect GetArea() const;
  int GetTargetWidth() const;
  void LayoutWidget();
  void SetEscapeRegistered(bool registered);

  const raw_ptr<BrowserView> browser_view_;
  // Hands the touchpad's swipes over the panel to the Spaces' swipe (macOS).
  // Outlives the widget, whose root view has it.
  std::unique_ptr<ui::EventHandler> swipe_handler_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<KoanLibraryView> view_ = nullptr;
  LibrarySection section_;
  bool open_ = false;
  bool keeping_open_ = false;
  // A swipe holds the panel (StartSwipe()).
  bool swiping_ = false;

  // Open/close spring: progress runs from `from_` to `target_`.
  gfx::LinearAnimation slide_{this};
  scoped_refptr<gfx::AnimationContainer> frames_container_;
  // The sidebar's children the Library gave a layer to move them.
  std::vector<std::unique_ptr<views::ViewTracker>> sidebar_layered_;
  double progress_ = 0;
  double from_ = 0;
  double target_ = 0;

  // The panel's width follows its section, eased like Zen's
  // `transition: width 0.3s`.
  gfx::LinearAnimation resize_{this};
  int width_ = 0;
  // Whether the sidebar, and so the library, is on the screen's right.
  bool on_screen_right_ = false;
  int width_from_ = 0;
  int width_to_ = 0;

  base::OneShotTimer cleanup_timer_;
  int keep_alive_count_ = 0;
  raw_ptr<views::FocusManager> escape_focus_manager_ = nullptr;

  base::WeakPtrFactory<KoanLibraryController> weak_factory_{this};
};

// The Library button at the bottom of the sidebar (Zen's zen-library-button,
// first in the sidebar's footer row). Null if the window has no Library.
std::unique_ptr<views::View> CreateLibraryButton(BrowserView* browser_view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_H_
