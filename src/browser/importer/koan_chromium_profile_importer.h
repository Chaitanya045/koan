// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_IMPORTER_KOAN_CHROMIUM_PROFILE_IMPORTER_H_
#define KOAN_BROWSER_IMPORTER_KOAN_CHROMIUM_PROFILE_IMPORTER_H_

#include <stdint.h>

#include "base/memory/weak_ptr.h"
#include "components/user_data_importer/common/importer_data_types.h"

class ExternalProcessImporterHost;
class ProfileWriter;

namespace koan::importer {

// The hook in ExternalProcessImporterHost::LaunchImportIfReady(), once the
// bookmarks and search engines have loaded: imports a Chromium-format
// profile (Arc, Chrome...; koan_import_sources.h) in the browser process,
// where Chromium would start its utility process importer, which has no
// importer for such a profile on macOS. Reads it on the thread pool
// (koan_chromium_profile_reader.h; the keychain for passwords), then hands
// the data to `writer` through an InProcessImporterBridge and tells `host`
// the import's progress as the utility process would, so the import dialog
// and the first-run import work unchanged. Nothing is written once `host`
// is gone (the import cancelled). Returns false, doing nothing, for any
// other source.
bool MaybeStartChromiumProfileImport(
    const user_data_importer::SourceProfile& source,
    uint16_t items,
    ProfileWriter* writer,
    base::WeakPtr<ExternalProcessImporterHost> host);

}  // namespace koan::importer

#endif  // KOAN_BROWSER_IMPORTER_KOAN_CHROMIUM_PROFILE_IMPORTER_H_
