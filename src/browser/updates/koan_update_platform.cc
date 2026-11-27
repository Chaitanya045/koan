// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/updates/koan_update_platform.h"

#include "build/build_config.h"

namespace koan::updates {

PrepareRequest::PrepareRequest() = default;
PrepareRequest::PrepareRequest(const PrepareRequest&) = default;
PrepareRequest& PrepareRequest::operator=(const PrepareRequest&) = default;
PrepareRequest::~PrepareRequest() = default;

HelperRequest::HelperRequest() = default;
HelperRequest::HelperRequest(const HelperRequest&) = default;
HelperRequest& HelperRequest::operator=(const HelperRequest&) = default;
HelperRequest::~HelperRequest() = default;

#if !BUILDFLAG(IS_MAC)
// Linux's Flatpak can't replace itself (#548): notices only.
std::unique_ptr<InstallPlatform> CreateInstallPlatform() {
  return nullptr;
}
#endif

}  // namespace koan::updates
