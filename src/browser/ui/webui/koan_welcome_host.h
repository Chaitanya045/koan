// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_WEBUI_KOAN_WELCOME_HOST_H_
#define KOAN_BROWSER_UI_WEBUI_KOAN_WELCOME_HOST_H_

#include <optional>
#include <vector>

#include "base/functional/callback.h"
#include "content/public/browser/web_contents_user_data.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "url/gurl.h"

namespace koan {

// What chrome://koan-welcome asks of the window it is shown over: the
// welcome screen (KoanWelcome) attaches this to the page's WebContents. A
// page opened any other way (typed into a tab) has none: it shows nothing and
// changes nothing (KoanWelcomeHandler).
class KoanWelcomeHost : public content::WebContentsUserData<KoanWelcomeHost> {
 public:
  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();

    // The import page picked a browser: Chromium's import dialog opens once
    // the screen has closed (over the screen it would be hidden).
    base::RepeatingClosure import;
    // The colours page: the active Space's colour (a tab group colour).
    base::RepeatingCallback<void(int color_id)> space_color;
    // The colours page with Koan's theme picker (#41), as Zen opens its own
    // over the page: its size (none: no picker, the page shows its colours),
    // opening it over `anchor` (in the page's coordinates) and closing it.
    base::RepeatingCallback<std::optional<gfx::Size>()> theme_picker_size;
    base::RepeatingCallback<void(const gfx::Rect& anchor)> show_theme_picker;
    base::RepeatingClosure close_theme_picker;
    // The system's accent colour (Zen's AccentColor), if it has one.
    base::RepeatingCallback<std::optional<SkColor>()> system_accent;
    // "Dive in!": pin `essentials` and close the screen.
    base::RepeatingCallback<void(std::vector<GURL> essentials)> finish;
    // A press outside the card: the window follows the pointer while the
    // button is down. Then, at the first move without it, `window_drag_end`
    // (macOS: AppKit's drag swallowed the page's release).
    base::RepeatingClosure window_drag;
    base::RepeatingClosure window_drag_end;
    // Esc: close the screen, applying nothing more.
    base::RepeatingClosure skip;
  };

  KoanWelcomeHost(const KoanWelcomeHost&) = delete;
  KoanWelcomeHost& operator=(const KoanWelcomeHost&) = delete;
  ~KoanWelcomeHost() override;

  const Callbacks& callbacks() const { return callbacks_; }

 private:
  friend class content::WebContentsUserData<KoanWelcomeHost>;
  KoanWelcomeHost(content::WebContents* contents, Callbacks callbacks);

  Callbacks callbacks_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_WEBUI_KOAN_WELCOME_HOST_H_
