// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/koan_new_tab_page.h"

#include "components/prefs/pref_service.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/common/koan_url_constants.h"

namespace koan {

GURL GetNewTabPageURL(const PrefService& prefs) {
  if (!features::IsNewTabEnabled() ||
      prefs.GetString(prefs::kNewTabPage) == prefs::kNewTabPageChromium) {
    return GURL();
  }
  return GURL(kNewTabURL);
}

}  // namespace koan
