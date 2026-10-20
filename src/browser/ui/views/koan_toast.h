// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_TOAST_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_TOAST_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"

class BrowserView;

namespace views {
class Widget;
}

namespace koan {

class KoanToastView;

// Zen's toast (ZenToasts.mjs, zen-popup.css) for one window: a pill at the
// window's top right that springs in from the top, stays for 2s once in
// (longer while hovered), then fades out shrinking. Showing another replaces
// it. Used for "Tabs Closed!" (sidebar tabs) and the split view limit.
class KoanToast {
 public:
  explicit KoanToast(BrowserView* browser_view);
  KoanToast(const KoanToast&) = delete;
  KoanToast& operator=(const KoanToast&) = delete;
  ~KoanToast();

  // Shows `text`, or with `shortcut`, "`text` [shortcut] `after`" with the
  // shortcut in a key cap (Zen's "Tabs Closed! Use [Ctrl+Shift+T] to
  // undo."). Screen readers get `announcement`, or the toast's text.
  void Show(const std::u16string& text,
            const std::u16string& shortcut = std::u16string(),
            const std::u16string& after = std::u16string(),
            const std::u16string& announcement = std::u16string());

  // Drops the toast at once (e.g. the window is going away).
  void Close();

  // Keeps the toast in the window's corner when the window is resized.
  void OnBrowserViewLayout();

 private:
  void OnHovered(bool hovered);
  void Hide();

  const raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<KoanToastView> view_ = nullptr;
  // Hides the toast, then closes it once it has faded out.
  base::OneShotTimer timer_;
  bool hiding_ = false;
  base::WeakPtrFactory<KoanToast> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_TOAST_H_
