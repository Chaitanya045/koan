// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_H_

#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "koan/browser/ui/views/koan_library_ui.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"

class BrowserView;

namespace views {
class FocusManager;
class View;
class Widget;
}  // namespace views

namespace koan {

class KoanLibraryView;

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

  // Runs `open_tab`, which opens a tab from inside the library, without that
  // tab closing the library (a page opened in the background).
  void KeepOpenWhile(base::OnceClosure open_tab);

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

 private:
  void CreateWidget();
  void DestroyWidget();
  // Slides the panel towards `target` (0 closed, 1 open) on Zen's spring.
  void AnimateTo(double target);
  void SetProgress(double progress);
  void Finish();
  // The space the library covers: the window below any toolbar row, from
  // the sidebar's edge.
  gfx::Rect GetArea() const;
  int GetTargetWidth() const;
  void LayoutWidget();
  void SetEscapeRegistered(bool registered);

  const raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<KoanLibraryView> view_ = nullptr;
  LibrarySection section_;
  bool open_ = false;
  bool keeping_open_ = false;

  // Open/close spring: progress runs from `from_` to `target_`.
  gfx::LinearAnimation slide_{this};
  double progress_ = 0;
  double from_ = 0;
  double target_ = 0;

  // The panel's width follows its section, eased like Zen's
  // `transition: width 0.3s`.
  gfx::LinearAnimation resize_{this};
  int width_ = 0;
  int width_from_ = 0;
  int width_to_ = 0;

  base::OneShotTimer cleanup_timer_;
  raw_ptr<views::FocusManager> escape_focus_manager_ = nullptr;

  base::WeakPtrFactory<KoanLibraryController> weak_factory_{this};
};

// The Library button at the bottom of the sidebar (Zen's zen-library-button,
// first in the sidebar's footer row). Null if the window has no Library.
std::unique_ptr<views::View> CreateLibraryButton(BrowserView* browser_view);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_H_
