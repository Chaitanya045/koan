// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_TOAST_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_TOAST_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"

class BrowserView;

namespace gfx {
struct VectorIcon;
}

namespace views {
class Widget;
}

namespace koan {

class KoanToastView;

// How long Zen's toasts stay once in (ZenToasts.mjs).
inline constexpr base::TimeDelta kToastDefaultTimeout = base::Seconds(2);

// Zen's toast (ZenToasts.mjs, zen-popup.css) for one window: a pill at the
// window's top corner away from the sidebar (top right, or top left with
// tabs on the right) that springs in from the top, stays for 2s once in
// (longer while hovered), then fades out shrinking. Showing another replaces
// it. Used for "Tabs Closed!" (sidebar tabs), the split view limit, "New
// background tab opened!" and "Workspace has been successfully renamed!".
class KoanToast {
 public:
  explicit KoanToast(BrowserView* browser_view);
  KoanToast(const KoanToast&) = delete;
  KoanToast& operator=(const KoanToast&) = delete;
  ~KoanToast();

  // Shows `text`, or with `shortcut`, "`text` [shortcut] `after`" with the
  // shortcut in a key cap (Zen's "Tabs Closed! Use [Ctrl+Shift+T] to
  // undo."). Screen readers get `announcement`, or the toast's text. It
  // stays `timeout` once in (Zen's default, or a caller's own).
  void Show(const std::u16string& text,
            const std::u16string& shortcut = std::u16string(),
            const std::u16string& after = std::u16string(),
            const std::u16string& announcement = std::u16string(),
            base::TimeDelta timeout = kToastDefaultTimeout);

  // Shows `text` with Zen's small icon button after it (e.g. "New background
  // tab opened!" with a tab icon that switches to the tab). `action_name`
  // is the button's tooltip and accessible name; pressing it runs `action`
  // and hides the toast.
  void ShowWithAction(const std::u16string& text,
                      const gfx::VectorIcon& action_icon,
                      const std::u16string& action_name,
                      base::OnceClosure action);

  // Drops the toast at once (e.g. the window is going away).
  void Close();

  // Keeps the toast in the window's corner when the window is resized.
  void OnBrowserViewLayout();

 private:
  // Shows `view`, saying `announcement` to screen readers.
  void ShowView(std::unique_ptr<KoanToastView> view,
                const std::u16string& announcement,
                base::TimeDelta timeout);
  void OnHovered(bool hovered);
  void RunAction();
  void Hide();

  const raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<KoanToastView> view_ = nullptr;
  // Hides the toast, then closes it once it has faded out.
  base::OneShotTimer timer_;
  base::TimeDelta timeout_;
  bool hiding_ = false;
  base::OnceClosure action_;
  base::WeakPtrFactory<KoanToast> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_TOAST_H_
