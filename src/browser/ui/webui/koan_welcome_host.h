// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_WEBUI_KOAN_WELCOME_HOST_H_
#define KOAN_BROWSER_UI_WEBUI_KOAN_WELCOME_HOST_H_

#include <vector>

#include "base/functional/callback.h"
#include "content/public/browser/web_contents_user_data.h"
#include "url/gurl.h"

namespace koan {

// What chrome://koan-welcome asks of the window it is shown over: the
// welcome screen (KoanWelcome) attaches this to the page's WebContents. A
// page opened any other way (typed into a tab) has none, and its buttons do
// nothing beyond the page.
class KoanWelcomeHost : public content::WebContentsUserData<KoanWelcomeHost> {
 public:
  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();

    // Import from another browser (Chromium's import dialog), now.
    base::RepeatingClosure import;
    // The colours page: the active Space's colour (a tab group colour).
    base::RepeatingCallback<void(int color_id)> space_color;
    // "Dive in!": pin `essentials` and close the screen.
    base::RepeatingCallback<void(std::vector<GURL> essentials)> finish;
    // A press outside the card: the window follows the pointer.
    base::RepeatingClosure window_drag;
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
