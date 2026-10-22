// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_SECTIONS_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_SECTIONS_H_

#include <memory>

namespace koan {

class KoanLibraryController;
class KoanLibrarySection;

// The Library's sections (koan_library_{downloads,spaces,history}.cc).
std::unique_ptr<KoanLibrarySection> CreateLibraryDownloadsSection(
    KoanLibraryController* library);
std::unique_ptr<KoanLibrarySection> CreateLibrarySpacesSection(
    KoanLibraryController* library);
std::unique_ptr<KoanLibrarySection> CreateLibraryHistorySection(
    KoanLibraryController* library);

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_LIBRARY_SECTIONS_H_
