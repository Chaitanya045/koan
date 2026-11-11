// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_reach.h"

#include <string_view>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace koan::agent {

namespace {

FrameFacts Http(std::string_view url) {
  FrameFacts facts;
  facts.committed_url = GURL(url);
  facts.site_url = GURL(url).GetWithEmptyPath();
  return facts;
}

}  // namespace

TEST(KoanAgentReachTest, ReachableUrls) {
  for (std::string_view url :
       {"", "https://example.com/a", "http://example.com/", "about:blank",
        "about:blank#x", "about:srcdoc", "chrome-error://chromewebdata/",
        "data:text/html,hi", "blob:https://example.com/0f3c"}) {
    EXPECT_TRUE(IsReachableFrameUrl(GURL(url))) << url;
  }
}

// T7: privileged pages and the agent control page itself are never reached.
TEST(KoanAgentReachTest, UnreachableUrls) {
  for (std::string_view url :
       {"chrome://settings", "chrome://settings/koan/agents",
        "chrome-untrusted://print", "chrome-extension://abc/popup.html",
        "devtools://devtools/bundled/inspector.html", "file:///etc/hosts",
        "view-source:https://example.com", "filesystem:https://e.com/t/a",
        "about:version", "about:settings", "blob:chrome://settings/0f3c",
        "blob:chrome-extension://abc/0f3c", "javascript:alert(1)",
        "chrome-error://other/", "ws://example.com", "ftp://example.com",
        "http://[bad"}) {
    EXPECT_FALSE(IsReachableFrameUrl(GURL(url))) << url;
  }
}

TEST(KoanAgentReachTest, EveryAncestorMustPass) {
  const std::vector<FrameFacts> ok = {Http("https://a.com/"),
                                      Http("https://b.com/")};
  EXPECT_EQ(MayReachFrame(ok), Reach::kAllowed);

  // An https frame inside a WebUI (an iframe in a chrome:// page).
  std::vector<FrameFacts> in_webui = ok;
  in_webui[1].web_ui = true;
  EXPECT_EQ(MayReachFrame(in_webui), Reach::kWebUi);

  // A frame whose principal already moved to a privileged site.
  std::vector<FrameFacts> moving = ok;
  moving[0].site_url = GURL("chrome://settings");
  EXPECT_EQ(MayReachFrame(moving), Reach::kWrongScheme);

  std::vector<FrameFacts> guest = ok;
  guest[1].pdf_or_guest = true;
  EXPECT_EQ(MayReachFrame(guest), Reach::kPdfOrGuest);

  std::vector<FrameFacts> privileged = ok;
  privileged[0].privileged_contents = true;
  EXPECT_EQ(MayReachFrame(privileged), Reach::kPrivileged);

  std::vector<FrameFacts> interstitial = ok;
  interstitial[1].interstitial = true;
  EXPECT_EQ(MayReachFrame(interstitial), Reach::kInterstitial);

  std::vector<FrameFacts> incognito = ok;
  incognito[1].off_the_record_or_system = true;
  EXPECT_EQ(MayReachFrame(incognito), Reach::kOffTheRecord);

  std::vector<FrameFacts> extension_parent = ok;
  extension_parent[1] = Http("chrome-extension://abc/page.html");
  EXPECT_EQ(MayReachFrame(extension_parent), Reach::kWrongScheme);

  EXPECT_EQ(MayReachFrame({}), Reach::kWrongScheme);
}

// A failed navigation commits the error page: the tab stays reachable
// (review finding 3).
TEST(KoanAgentReachTest, TheErrorPageDoesNotDetach) {
  FrameFacts error;
  error.committed_url = GURL("https://does-not-resolve.example/");
  error.site_url = GURL("chrome-error://chromewebdata/");
  const FrameFacts chain[] = {error};
  EXPECT_EQ(MayReachFrame(chain), Reach::kAllowed);
  EXPECT_TRUE(MayAttachToUrl(GURL("chrome-error://chromewebdata/"), false));
}

TEST(KoanAgentReachTest, MayAttachToUrlRefusesWebUi) {
  EXPECT_TRUE(MayAttachToUrl(GURL("https://example.com/"), false));
  EXPECT_FALSE(MayAttachToUrl(GURL("https://example.com/"), true));
  EXPECT_FALSE(MayAttachToUrl(GURL("chrome://newtab/"), false));
}

}  // namespace koan::agent
