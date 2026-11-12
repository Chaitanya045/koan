// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_COLOR_SCHEME_SYNC_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_COLOR_SCHEME_SYNC_H_

#include "base/memory/raw_ptr.h"
#include "components/prefs/pref_change_registrar.h"

class Profile;

namespace koan {

// In the shadcn style the look's mode (koan.look.mode, Settings > Look and
// Feel > Mode) decides light or dark for every window
// (koan::AdjustColorProviderKey()), over the profile's colour scheme. Two
// controls write that scheme instead: Chromium's Appearance > Mode row and
// the macOS View > Appearance menu (both ThemeService::SetBrowserColorScheme).
// So that they do what they say, and show the mode in use, this keeps the
// two in step for one window's profile while the shadcn style is on: a
// change of the scheme becomes the look's mode, and a change of the look's
// mode (or a switch to the shadcn style) becomes the scheme. Not at start:
// the scheme is also web pages' prefers-color-scheme, which the look's
// default must not change by itself. In the Zen style the scheme decides by
// itself.
// Each window has one; they agree, so the second write is a no-op.
class KoanColorSchemeSync {
 public:
  // `profile` is the window's; nothing is kept in step for an off-the-record
  // one (its windows are dark).
  explicit KoanColorSchemeSync(Profile* profile);
  KoanColorSchemeSync(const KoanColorSchemeSync&) = delete;
  KoanColorSchemeSync& operator=(const KoanColorSchemeSync&) = delete;
  ~KoanColorSchemeSync();

 private:
  void OnSchemeChanged();
  void OnLookChanged();

  raw_ptr<Profile> profile_ = nullptr;
  PrefChangeRegistrar profile_registrar_;
  PrefChangeRegistrar local_state_registrar_;
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_COLOR_SCHEME_SYNC_H_
