// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_COLOR_KOAN_LOOK_H_
#define KOAN_BROWSER_UI_COLOR_KOAN_LOOK_H_

#include "koan/browser/ui/color/koan_tokens.h"

namespace koan::look {

// Told when the look changes, on the UI thread.
class Observer {
 public:
  virtual void OnLookChanged() = 0;

 protected:
  ~Observer() = default;
};

// The look every window draws with: Koan's style (shadcn's design system or
// Zen's), the mode, the base colour, the accent and a pasted theme. It is
// browser-wide, because colours are mixed per colour-provider key, which
// knows no profile. The user's choice lives in local-state prefs
// (koan_prefs.h) and is loaded by koan_look_controller.h; until that has
// run, this is the default: shadcn, dark, Neutral.
//
// UI thread only.
const Look& CurrentLook();

// Changes the look. Returns false if it is the current one.
bool SetLook(Look look);

bool IsShadcnStyle();

// The current look's tokens for light or dark, cached until the look changes.
const TokenSet& CurrentTokens(bool dark);

// Shortcuts for views that need a shape: the current radius scale.
int CurrentRadiusSm();
int CurrentRadiusMd();
int CurrentRadiusLg();
int CurrentRadiusXl();

void AddObserver(Observer* observer);
void RemoveObserver(Observer* observer);

}  // namespace koan::look

#endif  // KOAN_BROWSER_UI_COLOR_KOAN_LOOK_H_
