// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_IMPORTER_KOAN_CHROMIUM_PROFILE_IMPORTER_H_
#define KOAN_BROWSER_IMPORTER_KOAN_CHROMIUM_PROFILE_IMPORTER_H_

#include <stdint.h>

#include "base/memory/weak_ptr.h"
#include "components/user_data_importer/common/importer_data_types.h"

class ExternalProcessImporterHost;
class Profile;
class ProfileWriter;

namespace koan::importer {

// The hook in ExternalProcessImporterHost::LaunchImportIfReady(), once the
// bookmarks and search engines have loaded, where Chromium would start its
// utility process importer:
// - A Chromium-format profile (Arc, Chrome...; koan_import_sources.h), which
//   Chromium 158 has no importer for on macOS, is imported in the browser
//   process: read on the thread pool (koan_chromium_profile_reader.h; the
//   keychain for passwords), then handed to `writer` through an
//   InProcessImporterBridge, with `host` told the import's progress as the
//   utility process would, so the import dialog and the first-run import
//   work unchanged.
// - Koan's "Spaces and pinned tabs" (kSpacesImportItem in `items`: Arc's
//   and Zen's Spaces, pinned tabs, folders and Essentials) are read on the
//   thread pool and made in `profile`'s window (koan_space_import.h), as
//   Zen's import does. For Zen the item is taken out of `items`, so that
//   Chromium's Firefox importer brings the rest meanwhile; asked for alone,
//   it is the whole import.
// Nothing is written once `host` is gone (the import cancelled), but for
// Zen's Spaces next to Chromium's own import, which ends on its own. Returns
// whether the import is Koan's (the host then starts nothing); false for
// any other source, `items` unchanged.
bool MaybeStartKoanImport(const user_data_importer::SourceProfile& source,
                          uint16_t& items,
                          Profile* profile,
                          ProfileWriter* writer,
                          base::WeakPtr<ExternalProcessImporterHost> host);

}  // namespace koan::importer

#endif  // KOAN_BROWSER_IMPORTER_KOAN_CHROMIUM_PROFILE_IMPORTER_H_
