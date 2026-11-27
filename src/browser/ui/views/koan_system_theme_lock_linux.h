// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_SYSTEM_THEME_LOCK_LINUX_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_SYSTEM_THEME_LOCK_LINUX_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_observer.h"

class Profile;

namespace koan {

// Linux: keeps a window's profile on Chromium's classic theme, never on the
// GTK or Qt one (ThemeServiceAuraLinux's system theme). A toolkit theme paints
// the frame, the toolbar and their text with its own colours, so the sidebar
// was the toolkit's colour while the rest of Koan followed the look and its
// colour mode: in the Flatpak, light Adwaita (the sandbox can't see the
// host's theme) under Koan's dark text (#486). With the classic theme the
// frame is Koan's (kColorSysHeader, the look's `sidebar`), as on the Mac, and
// light or dark follows the desktop's colour-scheme setting through the
// portal, live.
// New profiles start there (koan::prefs::OverrideSystemThemeDefault()); this
// moves a profile that is on a toolkit theme (one that chose it before, or
// Chromium's "Use GTK" / "Use Qt", whose Settings row Koan hides) back to
// the classic one, as Chromium's "Use Classic" does. Each window has one;
// the second does nothing.
class KoanSystemThemeLock : public ThemeServiceObserver {
 public:
  // `profile` is the window's. An off-the-record window already ignores the
  // toolkit theme (BrowserWidget::SelectNativeTheme()), and its profile's
  // theme service is the original's.
  explicit KoanSystemThemeLock(Profile* profile);
  KoanSystemThemeLock(const KoanSystemThemeLock&) = delete;
  KoanSystemThemeLock& operator=(const KoanSystemThemeLock&) = delete;
  ~KoanSystemThemeLock() override;

  // ThemeServiceObserver:
  void OnThemeChanged() override;

 private:
  // Posted: the theme service may be switching suppliers (a toolkit theme's
  // StartUsingTheme() notifies first), and the window may still be being
  // built.
  void LeaveSystemThemeSoon();
  void LeaveSystemTheme();

  raw_ptr<ThemeService> theme_service_ = nullptr;
  base::ScopedObservation<ThemeService, ThemeServiceObserver> observation_{
      this};
  base::WeakPtrFactory<KoanSystemThemeLock> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_SYSTEM_THEME_LOCK_LINUX_H_
