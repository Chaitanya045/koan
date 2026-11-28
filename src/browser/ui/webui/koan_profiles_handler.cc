// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_profiles_handler.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/ui/views/koan_space_profiles.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"

namespace koan {

namespace {

const std::string* StringArg(const base::ListValue& args, size_t index) {
  return index < args.size() ? args[index].GetIfString() : nullptr;
}

}  // namespace

KoanProfilesHandler::KoanProfilesHandler(Profile& profile)
    : profile_(profile) {}

KoanProfilesHandler::~KoanProfilesHandler() = default;

void KoanProfilesHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "koanGetProfiles", base::BindRepeating(&KoanProfilesHandler::HandleGet,
                                             base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "koanGetProfileSizes",
      base::BindRepeating(&KoanProfilesHandler::HandleGetSizes,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "koanRenameProfile",
      base::BindRepeating(&KoanProfilesHandler::HandleRename,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "koanSetProfileColor",
      base::BindRepeating(&KoanProfilesHandler::HandleSetColor,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "koanClearProfileData",
      base::BindRepeating(&KoanProfilesHandler::HandleClearData,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "koanDeleteProfile",
      base::BindRepeating(&KoanProfilesHandler::HandleDelete,
                          base::Unretained(this)));
}

void KoanProfilesHandler::OnJavascriptAllowed() {
  pref_change_registrar_.Init(profile_->GetPrefs());
  pref_change_registrar_.Add(
      prefs::kUserContainers,
      base::BindRepeating(&KoanProfilesHandler::OnProfilesChanged,
                          base::Unretained(this)));
}

void KoanProfilesHandler::OnJavascriptDisallowed() {
  pref_change_registrar_.Reset();
  // Replies still on their way were for the page that went.
  weak_factory_.InvalidateWeakPtrs();
}

void KoanProfilesHandler::HandleGet(const base::ListValue& args) {
  CHECK_EQ(args.size(), 1u);
  AllowJavascript();
  ResolveJavascriptCallback(args[0], Profiles());
}

void KoanProfilesHandler::HandleGetSizes(const base::ListValue& args) {
  CHECK_EQ(args.size(), 1u);
  AllowJavascript();
  GetSpaceProfileSizes(
      &*profile_, base::BindOnce(&KoanProfilesHandler::OnSizes,
                                 weak_factory_.GetWeakPtr(), args[0].Clone()));
}

void KoanProfilesHandler::HandleRename(const base::ListValue& args) {
  CHECK_EQ(args.size(), 3u);
  AllowJavascript();
  const std::string* id = StringArg(args, 1);
  const std::string* name = StringArg(args, 2);
  ResolveJavascriptCallback(
      args[0],
      id && name &&
          RenameSpaceProfile(&*profile_, *id, base::UTF8ToUTF16(*name)));
}

void KoanProfilesHandler::HandleSetColor(const base::ListValue& args) {
  CHECK_EQ(args.size(), 3u);
  AllowJavascript();
  const std::string* id = StringArg(args, 1);
  const std::optional<int> color = args[2].GetIfInt();
  ResolveJavascriptCallback(
      args[0], id && color && SetSpaceProfileColor(&*profile_, *id, *color));
}

void KoanProfilesHandler::HandleClearData(const base::ListValue& args) {
  CHECK_EQ(args.size(), 2u);
  AllowJavascript();
  const std::string* id = StringArg(args, 1);
  if (!id || !ClearSpaceProfileData(
                 &*profile_, *id,
                 base::BindOnce(&KoanProfilesHandler::Resolve,
                                weak_factory_.GetWeakPtr(), args[0].Clone(),
                                /*ok=*/true))) {
    ResolveJavascriptCallback(args[0], false);
  }
}

void KoanProfilesHandler::HandleDelete(const base::ListValue& args) {
  CHECK_EQ(args.size(), 2u);
  AllowJavascript();
  const std::string* id = StringArg(args, 1);
  if (!id || !DeleteSpaceProfile(
                 &*profile_, *id,
                 base::BindOnce(&KoanProfilesHandler::Resolve,
                                weak_factory_.GetWeakPtr(), args[0].Clone(),
                                /*ok=*/true))) {
    ResolveJavascriptCallback(args[0], false);
  }
}

void KoanProfilesHandler::OnSizes(base::Value callback_id,
                                  base::flat_map<std::string, int64_t> sizes) {
  if (!IsJavascriptAllowed()) {
    return;
  }
  base::DictValue bytes;
  for (const auto& [id, size] : sizes) {
    // (A double: base::Value has no 64-bit integers.)
    bytes.Set(id, static_cast<double>(size));
  }
  ResolveJavascriptCallback(callback_id, bytes);
}

void KoanProfilesHandler::Resolve(base::Value callback_id, bool ok) {
  if (IsJavascriptAllowed()) {
    ResolveJavascriptCallback(callback_id, ok);
  }
}

void KoanProfilesHandler::OnProfilesChanged() {
  if (IsJavascriptAllowed()) {
    FireWebUIListener("koan-profiles-changed", Profiles());
  }
}

base::DictValue KoanProfilesHandler::Profiles() {
  base::ListValue profiles;
  for (const SpaceProfileInfo& info : GetSpaceProfiles(&*profile_)) {
    base::ListValue spaces;
    for (const std::u16string& space : info.spaces) {
      spaces.Append(space);
    }
    profiles.Append(base::DictValue()
                        .Set("id", info.id)
                        .Set("name", info.name)
                        .Set("color", info.color)
                        .Set("spaces", std::move(spaces))
                        .Set("openTabs", info.open_tabs)
                        .Set("essentials", info.essentials)
                        .Set("pinnedTabs", info.pinned_tabs));
  }
  // The swatches in the colours the browser draws them in (light or dark).
  const ui::ColorProvider& provider =
      web_ui()->GetWebContents()->GetColorProvider();
  base::ListValue colors;
  for (const SpaceProfileColor& color : GetSpaceProfileColors()) {
    colors.Append(base::DictValue()
                      .Set("color", color.color)
                      .Set("name", color.name)
                      .Set("css", color_utils::SkColorToRgbaString(
                                      provider.GetColor(color.dot))));
  }
  return base::DictValue()
      .Set("profiles", std::move(profiles))
      .Set("colors", std::move(colors));
}

}  // namespace koan
