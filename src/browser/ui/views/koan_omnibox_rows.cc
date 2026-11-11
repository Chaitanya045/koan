// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   zen-omnibox.css (the suggestion rows' height, radius and font; an open
//   tab's "Switch to Tab")
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_omnibox_rows.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

#include "base/no_destructor.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "components/omnibox/browser/actions/omnibox_action.h"
#include "koan/browser/ui/color/koan_color_id.h"
#include "koan/browser/ui/color/koan_look.h"
#include "koan/browser/ui/vector_icons/vector_icons.h"
#include "koan/browser/ui/views/koan_omnibox_action_label.h"
#include "koan/browser/ui/views/koan_sidebar_metrics.h"
#include "koan/browser/ui/views/koan_url_bar.h"
#include "ui/base/class_property.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/font.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/style/typography.h"
#include "ui/views/view.h"

namespace koan {

namespace {

// The action button shows an open tab's tab switch action.
DEFINE_UI_CLASS_PROPERTY_KEY(bool, kTabSwitchButtonKey, false)
// The button has Zen's look (StyleOmniboxActionButton()), to undo if it
// changes action or leaves Zen's card.
DEFINE_UI_CLASS_PROPERTY_KEY(bool, kZenActionLookKey, false)

bool HasZenActionLook(const views::MdTextButton* button) {
  return button->GetProperty(kTabSwitchButtonKey) && ShowsZenCardRows(button);
}

}  // namespace

int OmniboxRowHeight(const views::View* view, int default_height) {
  return ShowsZenCardRows(view) ? kOmniboxRowHeight : default_height;
}

void AdjustOmniboxRowRadii(const views::View* view,
                           gfx::RoundedCornersF& radii) {
  if (ShowsZenCardRows(view)) {
    radii = gfx::RoundedCornersF(kOmniboxRowCornerRadius);
  }
}

gfx::FontList AdjustOmniboxRowFont(const views::View* view,
                                   const gfx::FontList& font) {
  if (!ShowsZenCardRows(view)) {
    return font;
  }
  // A row's views set their font on every text change; Zen's variant of each
  // base font is derived once.
  using Key = std::tuple<int, int, std::string>;
  static base::NoDestructor<std::map<Key, gfx::FontList>> cache;
  const Key key(font.GetFontSize(), static_cast<int>(font.GetFontWeight()),
                font.GetPrimaryFont().GetFontName());
  auto it = cache->find(key);
  if (it == cache->end()) {
    it = cache
             ->emplace(key, font.DeriveWithSizeDelta(kOmniboxRowFontSize -
                                                     font.GetFontSize())
                                .DeriveWithWeight(gfx::Font::Weight::MEDIUM))
             .first;
  }
  return it->second;
}

ui::ColorId AdjustOmniboxRowTextColorId(const views::View* view,
                                        ui::ColorId id) {
  return id == kColorOmniboxText && ShowsZenCardRows(view)
             ? ui::ColorId(kColorKoanOmniboxRowTitle)
             : id;
}

void LabelOmniboxActionButton(views::MdTextButton* button,
                              const OmniboxAction& action) {
  const std::optional<std::u16string_view> label = OmniboxActionLabel(action);
  button->SetProperty(kTabSwitchButtonKey,
                      action.ActionId() == OmniboxActionId::TAB_SWITCH);
  if (label) {
    button->SetText(*label);
    // Chromium's tooltip only repeats its own label; Zen's action has none.
    button->SetTooltipText(std::u16string());
  }
}

void StyleOmniboxActionButton(views::MdTextButton* button, bool selected) {
  if (!HasZenActionLook(button)) {
    if (button->GetProperty(kZenActionLookKey)) {
      button->ClearProperty(kZenActionLookKey);
      button->SetLabelStyle(views::style::STYLE_PRIMARY);
      button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    }
    return;
  }
  button->SetProperty(kZenActionLookKey, true);
  // Zen's weight is 600; Chromium's 14pt styles have 500 and 700.
  button->SetLabelStyle(views::style::STYLE_BODY_2_BOLD);
  // The keycap after the text, as Zen's ::after.
  button->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  // The row's text colour: Zen dims the action (opacity .7) until the row is
  // selected.
  const ui::ColorId color = selected ? kColorOmniboxResultsTextSelected
                                     : ui::ColorId(kColorKoanOmniboxRowTitle);
  button->SetEnabledTextColors(color);
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(kKoanUrlbarSwitchTabIcon, color,
                                     kOmniboxActionKeycapSize));
  // The row's own hover and selection are the only highlight.
  views::InkDrop::Get(button)->SetMode(views::InkDropHost::InkDropMode::OFF);
}

bool ClearsOmniboxActionButtonPill(views::MdTextButton* button) {
  if (!HasZenActionLook(button)) {
    return false;
  }
  button->SetBackground(nullptr);
  return true;
}

}  // namespace koan
