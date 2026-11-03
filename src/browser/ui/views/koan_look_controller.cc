// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/views/chrome_layout_provider.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/color/koan_color_mixer.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/fonts/koan_fonts.h"
#include "koan/browser/ui/views/koan_look_hooks.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/color/color_mixers.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/font_list.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/layout/layout_provider.h"

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
    next.style =
        look::StyleFromString(local_state->GetString(prefs::kLookStyle))
            .value_or(look::Style::kShadcn);
    next.mode = look::ModeFromString(local_state->GetString(prefs::kLookMode))
                    .value_or(look::Mode::kDark);
    next.base =
        look::BaseColorFromString(local_state->GetString(prefs::kLookBase))
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

// Geist is shadcn's typeface: the native UI's default font in the shadcn
// style, from the next start (fonts are loaded once, and the switch is
// rare). macOS only: Koan ships nowhere else.
void ApplyDefaultFont() {
#if BUILDFLAG(IS_MAC)
  if (!look::IsShadcnStyle() || !fonts::RegisterBundledFonts()) {
    return;
  }
  gfx::FontList::SetDefaultFontDescription(std::string(fonts::kSansFamily) +
                                           ", 13px");
  ui::ResourceBundle::GetSharedInstance().ReloadFonts();
#endif
}

LookController& GetController() {
  static base::NoDestructor<LookController> controller;
  return *controller;
}

// shadcn's shape for what Chromium rounds: buttons, textfields, comboboxes and
// menus are `rounded-md`, dialogs and popovers `rounded-lg`, cards and the
// side panel `rounded-xl`.
std::optional<look::Step> ShapeStep(views::ShapeContextToken id) {
  using Step = look::Step;
  switch (id) {
    case views::kBadgeRadius:
    case kExtensionsMenuButtonRadius:
      return Step::kSm;
    case views::kButtonRadius:
    case views::kComboboxRadius:
    case views::kMenuRadius:
    case views::kMenuAuxRadius:
    case views::kMenuTouchRadius:
    case views::kTextfieldRadius:
    case views::kContentSeparatorRadius:
    case kActionAppMenuFooterButtonCornerRadius:
      return Step::kMd;
    case views::kDialogRadius:
    case kFindBarViewRadius:
    case kRecentActivityContainerRadius:
    case kFeatureFirstRunInfoBoxRoundedBorderRadius:
    case kActionAppMenuContainerCornerRadius:
    case kActionAppMenuBlockEntryCornerRadius:
      return Step::kLg;
    case kSidePanelContentRadius:
    case kOmniboxExpandedRadius:
      return Step::kXl;
    default:
      return std::nullopt;
  }
}

}  // namespace

std::optional<int> GetLookCornerRadius(int token) {
  if (!look::IsShadcnStyle()) {
    return std::nullopt;
  }
  const std::optional<look::Step> step = ShapeStep(token);
  return step ? std::optional<int>(look::Radius(*step)) : std::nullopt;
}

void AdjustColorProviderKey(const Profile* profile, ui::ColorProviderKey& key) {
  static bool installed = false;
  if (!installed) {
    installed = true;
    ui::SetCoreColorMixerOverride(&AddKoanCoreColorMixer);
    // A provider built before this (for a window that isn't a browser's) has
    // the old colours.
    ui::ColorProviderManager::Get().ResetColorProviderCache();
    GetController().EnsureInitialized();
    ApplyDefaultFont();
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
