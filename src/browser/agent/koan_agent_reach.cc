// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_reach.h"

#include "url/url_constants.h"

namespace koan::agent {

namespace {

// content::kUnreachableWebDataURL, spelled out so that this target needs no
// //content. A unit test pins the two together in the content adapter.
constexpr char kNetworkErrorPage[] = "chrome-error://chromewebdata/";

bool IsAboutDocument(const GURL& url) {
  return url.SchemeIs(url::kAboutScheme) &&
         (url.IsAboutBlank() || url.IsAboutSrcdoc());
}

}  // namespace

bool IsReachableFrameUrl(const GURL& url) {
  if (url.is_empty()) {
    return true;
  }
  if (!url.is_valid()) {
    return false;
  }
  if (url.SchemeIsHTTPOrHTTPS() || IsAboutDocument(url) ||
      url.SchemeIs(url::kDataScheme)) {
    return true;
  }
  if (url.SchemeIsBlob()) {
    const GURL inner(url.GetContent());
    return inner.is_valid() && inner.SchemeIsHTTPOrHTTPS();
  }
  return url.spec() == kNetworkErrorPage;
}

Reach MayReachFrame(base::span<const FrameFacts> frame_and_ancestors) {
  if (frame_and_ancestors.empty()) {
    return Reach::kWrongScheme;
  }
  for (const FrameFacts& frame : frame_and_ancestors) {
    if (frame.off_the_record_or_system) {
      return Reach::kOffTheRecord;
    }
    if (frame.web_ui) {
      return Reach::kWebUi;
    }
    if (frame.privileged_contents) {
      return Reach::kPrivileged;
    }
    if (frame.pdf_or_guest) {
      return Reach::kPdfOrGuest;
    }
    if (frame.interstitial) {
      return Reach::kInterstitial;
    }
    if (!IsReachableFrameUrl(frame.committed_url) ||
        !IsReachableFrameUrl(frame.site_url)) {
      return Reach::kWrongScheme;
    }
  }
  return Reach::kAllowed;
}

bool MayAttachToUrl(const GURL& url, bool is_webui) {
  return !is_webui && IsReachableFrameUrl(url);
}

}  // namespace koan::agent
