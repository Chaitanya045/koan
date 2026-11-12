// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_color_scheme_sync.h"

#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/color/koan_tokens.h"

namespace koan {

namespace {

using Scheme = ThemeService::BrowserColorScheme;

PrefService* LocalState() {
  return g_browser_process ? g_browser_process->local_state() : nullptr;
}

// Read from the prefs, not look::CurrentLook(): this may be told of a change
// before the look is.
bool IsShadcnStyle(PrefService* local_state) {
  return look::StyleFromString(local_state->GetString(prefs::kLookStyle))
             .value_or(look::Style::kShadcn) == look::Style::kShadcn;
}

look::Mode ModeOf(Scheme scheme) {
  switch (scheme) {
    case Scheme::kLight:
      return look::Mode::kLight;
    case Scheme::kDark:
      return look::Mode::kDark;
    case Scheme::kSystem:
      return look::Mode::kAuto;
  }
  return look::Mode::kAuto;
}

Scheme SchemeOf(look::Mode mode) {
  switch (mode) {
    case look::Mode::kLight:
      return Scheme::kLight;
    case look::Mode::kDark:
      return Scheme::kDark;
    case look::Mode::kAuto:
      return Scheme::kSystem;
  }
  return Scheme::kSystem;
}

}  // namespace

KoanColorSchemeSync::KoanColorSchemeSync(Profile* profile) {
  PrefService* local_state = LocalState();
  if (!profile || profile->IsOffTheRecord() || !local_state ||
      !ThemeServiceFactory::GetForProfile(profile)) {
    return;
  }
  profile_ = profile;
  profile_registrar_.Init(profile->GetPrefs());
  profile_registrar_.Add(
      ::prefs::kBrowserColorScheme,
      base::BindRepeating(&KoanColorSchemeSync::OnSchemeChanged,
                          base::Unretained(this)));
  local_state_registrar_.Init(local_state);
  for (const char* pref : {prefs::kLookStyle, prefs::kLookMode}) {
    local_state_registrar_.Add(
        pref, base::BindRepeating(&KoanColorSchemeSync::OnLookChanged,
                                  base::Unretained(this)));
  }
  // The look's mode is what the windows show.
  OnLookChanged();
}

KoanColorSchemeSync::~KoanColorSchemeSync() = default;

void KoanColorSchemeSync::OnSchemeChanged() {
  PrefService* local_state = LocalState();
  ThemeService* theme_service = ThemeServiceFactory::GetForProfile(profile_);
  if (!local_state || !theme_service || !IsShadcnStyle(local_state)) {
    return;
  }
  const std::string_view mode =
      look::ModeToString(ModeOf(theme_service->GetBrowserColorScheme()));
  if (local_state->GetString(prefs::kLookMode) != mode) {
    local_state->SetString(prefs::kLookMode, std::string(mode));
  }
}

void KoanColorSchemeSync::OnLookChanged() {
  PrefService* local_state = LocalState();
  ThemeService* theme_service = ThemeServiceFactory::GetForProfile(profile_);
  if (!local_state || !theme_service || !IsShadcnStyle(local_state)) {
    return;
  }
  const Scheme scheme =
      SchemeOf(look::ModeFromString(local_state->GetString(prefs::kLookMode))
                   .value_or(look::Mode::kDark));
  if (theme_service->GetBrowserColorScheme() != scheme) {
    theme_service->SetBrowserColorScheme(scheme);
  }
}

}  // namespace koan
