// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/color/koan_color_mixer.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/views/koan_look_hooks.h"
#include "ui/color/color_mixers.h"
#include "ui/color/color_provider_manager.h"
#include "ui/color/color_provider_key.h"
#include "ui/native_theme/native_theme.h"

namespace koan {

namespace {

// Keeps koan::look::CurrentLook() in line with the local-state prefs, and
// makes every window follow a change at once.
class LookController : public look::Observer {
 public:
  LookController() = default;
  LookController(const LookController&) = delete;
  LookController& operator=(const LookController&) = delete;

  // Loads the prefs; a no-op once local state has been read.
  void EnsureInitialized() {
    if (initialized_ || !g_browser_process ||
        !g_browser_process->local_state()) {
      return;
    }
    initialized_ = true;
    PrefService* local_state = g_browser_process->local_state();
    registrar_.Init(local_state);
    for (const char* pref :
         {prefs::kLookStyle, prefs::kLookMode, prefs::kLookBase,
          prefs::kLookAccent, prefs::kLookCustomCss}) {
      registrar_.Add(pref, base::BindRepeating(&LookController::Reload,
                                               base::Unretained(this)));
    }
    Reload();
    // After the first load, so loading doesn't refresh anything.
    look::AddObserver(this);
  }

  // look::Observer:
  void OnLookChanged() override {
    // Providers cached for the old look are stale, including for a switch
    // that keeps the colour mode (shadcn <-> Zen).
    ui::NativeTheme::GetInstanceForNativeUi()->NotifyOnNativeThemeUpdated();
    // Browser windows get their colours from the profile's ThemeService.
    if (g_browser_process->profile_manager()) {
      for (Profile* profile :
           g_browser_process->profile_manager()->GetLoadedProfiles()) {
        ThemeService* service = ThemeServiceFactory::GetForProfile(profile);
        // Re-applies the profile's scheme, which tells every window of the
        // profile to fetch new colours.
        if (service) {
          service->SetBrowserColorScheme(service->GetBrowserColorScheme());
        }
      }
    }
  }

 private:
  void Reload() {
    PrefService* local_state = g_browser_process->local_state();
    look::Look next;
    next.style = look::StyleFromString(local_state->GetString(prefs::kLookStyle))
                     .value_or(look::Style::kShadcn);
    next.mode = look::ModeFromString(local_state->GetString(prefs::kLookMode))
                    .value_or(look::Mode::kDark);
    next.base = look::BaseColorFromString(local_state->GetString(prefs::kLookBase))
                    .value_or(look::BaseColor::kNeutral);
    next.accent =
        look::AccentFromString(local_state->GetString(prefs::kLookAccent))
            .value_or(look::Accent::kNone);
    // A pasted theme that doesn't parse is ignored.
    next.custom =
        look::ParseThemeCss(local_state->GetString(prefs::kLookCustomCss));
    look::SetLook(std::move(next));
  }

  bool initialized_ = false;
  PrefChangeRegistrar registrar_;
};

LookController& GetController() {
  static base::NoDestructor<LookController> controller;
  return *controller;
}

}  // namespace

void AdjustColorProviderKey(const Profile* profile,
                            ui::ColorProviderKey& key) {
  static bool installed = false;
  if (!installed) {
    installed = true;
    ui::SetCoreColorMixerOverride(&AddKoanCoreColorMixer);
    // A provider built before this (for a window that isn't a browser's) has
    // the old colours.
    ui::ColorProviderManager::Get().ResetColorProviderCache();
  }
  GetController().EnsureInitialized();
  if (!look::IsShadcnStyle() || profile->IsIncognitoProfile() ||
      profile->IsEnterpriseIsolatedModeProfile()) {
    return;
  }
  switch (look::CurrentLook().mode) {
    case look::Mode::kAuto:
      break;
    case look::Mode::kLight:
      key.color_mode = ui::ColorProviderKey::ColorMode::kLight;
      break;
    case look::Mode::kDark:
      key.color_mode = ui::ColorProviderKey::ColorMode::kDark;
      break;
  }
}

}  // namespace koan
