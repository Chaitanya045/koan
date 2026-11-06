// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_policy.h"

#include <map>
#include <set>
#include <string>
#include <string_view>

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace koan::agent {

namespace {

std::string DomainOf(std::string_view method) {
  return std::string(method.substr(0, method.find('.')));
}

}  // namespace

TEST(KoanAgentPolicyTest, TheTableIsSortedAndHasNoDuplicates) {
  std::string_view previous;
  for (const MethodRule& rule : MethodRules()) {
    EXPECT_LT(previous, rule.method) << rule.method;
    previous = rule.method;
  }
}

TEST(KoanAgentPolicyTest, EveryRuleIsDomainDotMethodAndGatesMatchVerdicts) {
  for (const MethodRule& rule : MethodRules()) {
    const size_t dot = rule.method.find('.');
    ASSERT_NE(dot, std::string_view::npos) << rule.method;
    EXPECT_GT(dot, 0u) << rule.method;
    EXPECT_GT(rule.method.size(), dot + 1) << rule.method;
    EXPECT_EQ(rule.method.find('.', dot + 1), std::string_view::npos)
        << rule.method;
    // Only gated methods name a capability, and they always do.
    EXPECT_EQ(rule.verdict == Verdict::kGated, rule.gate != Capability::kNone)
        << rule.method;
  }
}

TEST(KoanAgentPolicyTest, DeniedDomainsAreSortedAndNeverAlsoListed) {
  std::set<std::string> listed;
  for (const MethodRule& rule : MethodRules()) {
    listed.insert(DomainOf(rule.method));
  }
  std::string_view previous;
  for (std::string_view domain : DeniedDomains()) {
    EXPECT_LT(previous, domain) << domain;
    previous = domain;
    EXPECT_EQ(listed.count(std::string(domain)), 0u) << domain;
  }
}

TEST(KoanAgentPolicyTest, WhatAnAgentNeedsIsAllowed) {
  for (std::string_view method :
       {"Page.enable", "Page.navigate", "Page.captureScreenshot",
        "Page.addScriptToEvaluateOnNewDocument", "Page.startScreencast",
        "Runtime.enable", "Runtime.evaluate", "Runtime.callFunctionOn",
        "DOM.getDocument", "DOM.describeNode", "Accessibility.getFullAXTree",
        "Input.dispatchMouseEvent", "Input.dispatchKeyEvent",
        "Input.insertText", "Emulation.setDeviceMetricsOverride",
        "Network.enable", "Log.enable", "IO.read"}) {
    EXPECT_EQ(Classify(method).verdict, Verdict::kAllow) << method;
  }
}

TEST(KoanAgentPolicyTest, TheFacadeAnswersTheBrowserLevelMethods) {
  for (std::string_view method :
       {"Target.setDiscoverTargets", "Target.getTargets", "Target.createTarget",
        "Target.attachToTarget", "Target.setAutoAttach", "Target.closeTarget",
        "Browser.getVersion"}) {
    EXPECT_EQ(Classify(method).verdict, Verdict::kFacade) << method;
  }
}

TEST(KoanAgentPolicyTest, AgentTabsAreNeverRaised) {
  EXPECT_EQ(Classify("Page.bringToFront").verdict, Verdict::kNoOp);
  EXPECT_EQ(Classify("Target.activateTarget").verdict, Verdict::kNoOp);
}

TEST(KoanAgentPolicyTest,
     NetworkInterceptionAndBodiesNeedTheNetworkCapability) {
  for (std::string_view method :
       {"Fetch.enable", "Fetch.continueRequest", "Fetch.fulfillRequest",
        "Network.setExtraHTTPHeaders", "Network.getResponseBody",
        "Network.getRequestPostData"}) {
    const Decision decision = Classify(method);
    EXPECT_EQ(decision.verdict, Verdict::kGated) << method;
    EXPECT_EQ(decision.gate, Capability::kNetwork) << method;
  }
}

TEST(KoanAgentPolicyTest, CookiesStorageAndTheBrowserAreDenied) {
  for (std::string_view method : {
           "Network.getCookies",
           "Network.getAllCookies",
           "Network.setCookie",
           "Network.setCookies",
           "Network.deleteCookies",
           "Network.clearBrowserCookies",
           "Network.clearBrowserCache",
           "Network.loadNetworkResource",
           "Network.setRequestInterception",
           "Storage.getCookies",
           "Storage.clearCookies",
           "DOMStorage.getDOMStorageItems",
           "IndexedDB.requestData",
           "CacheStorage.requestCachedResponse",
           "Browser.close",
           "Browser.setDownloadBehavior",
           "Browser.grantPermissions",
           "Browser.setWindowBounds",
           "SystemInfo.getInfo",
           "Extensions.loadUnpacked",
           "Security.setIgnoreCertificateErrors",
           "Autofill.trigger",
           "Page.setDownloadBehavior",
           "Page.setWebLifecycleState",
           "Page.setBypassCSP",
           "Page.setInterceptFileChooserDialog",
           "Emulation.setFocusEmulationEnabled",
           "Emulation.setVirtualTimePolicy",
           "Input.setIgnoreInputEvents",
           "Input.dispatchDragEvent",
           "DOM.setFileInputFiles",
           "DOM.getFileInfo",
           "IO.resolveBlob",
           "Tracing.start",
           "Debugger.enable",
           "Target.createBrowserContext",
           "Target.attachToBrowserTarget",
           "Target.exposeDevToolsProtocol",
       }) {
    EXPECT_EQ(Classify(method).verdict, Verdict::kDeny) << method;
  }
}

TEST(KoanAgentPolicyTest, UnlistedMeansDeny) {
  for (std::string_view method :
       {"", ".", "Page", "Page.", ".navigate", "Page.noSuchMethod",
        "NoSuchDomain.method", "page.navigate", "PAGE.NAVIGATE",
        "Page.Navigate", " Page.navigate", "Page.navigate ", "Page.navigate\n",
        "Page.navigate.extra", "Page..navigate", "Page.nav",
        "Page.navigateX"}) {
    const Decision decision = Classify(method);
    EXPECT_EQ(decision.verdict, Verdict::kDeny) << method;
    EXPECT_EQ(decision.gate, Capability::kNone) << method;
  }
  // A name that only looks like an allowed one because of a NUL.
  const std::string with_nul("Page.navigate\0x", 15);
  EXPECT_EQ(Classify(with_nul).verdict, Verdict::kDeny);
  const std::string nul_prefix("\0Page.navigate", 14);
  EXPECT_EQ(Classify(nul_prefix).verdict, Verdict::kDeny);
}

TEST(KoanAgentPolicyTest, NoCookieOrStorageMethodIsEverAllowed) {
  for (const MethodRule& rule : MethodRules()) {
    if (rule.verdict != Verdict::kDeny) {
      EXPECT_EQ(rule.method.find("Cookie"), std::string_view::npos)
          << rule.method;
      EXPECT_EQ(rule.method.find("Storage"), std::string_view::npos)
          << rule.method;
      EXPECT_EQ(rule.method.find("loadNetworkResource"), std::string_view::npos)
          << rule.method;
    }
  }
}

// The rows that need a parameter filter before they are forwarded. A new
// allowed row that needs one must be added here, with its filter, in the same
// change that adds the filter: this list is what stops a gateway from treating
// kAllow as "forward unchanged".
TEST(KoanAgentPolicyTest, TheRowsThatNeedAFilterCarryIt) {
  const std::map<std::string, ParamFilter> expected = {
      {"Page.navigate", ParamFilter::kNavigationUrl},
      {"Target.createTarget", ParamFilter::kNavigationUrl},
      {"Input.dispatchKeyEvent", ParamFilter::kStripKeyCommands},
      {"Page.createIsolatedWorld", ParamFilter::kIsolatedWorld},
      {"Runtime.evaluate", ParamFilter::kRuntimeContext},
      {"Runtime.callFunctionOn", ParamFilter::kRuntimeContext},
      {"Runtime.addBinding", ParamFilter::kRuntimeContext},
      {"Page.captureScreenshot", ParamFilter::kScreenshot},
      {"Network.enable", ParamFilter::kNetworkEnable},
      {"IO.read", ParamFilter::kOwnStream},
      {"IO.close", ParamFilter::kOwnStream},
      {"Emulation.clearDeviceMetricsOverride", ParamFilter::kOwnTabOnly},
      {"Emulation.clearGeolocationOverride", ParamFilter::kOwnTabOnly},
      {"Emulation.setDefaultBackgroundColorOverride", ParamFilter::kOwnTabOnly},
      {"Emulation.setDeviceMetricsOverride", ParamFilter::kOwnTabOnly},
      {"Emulation.setEmulatedMedia", ParamFilter::kOwnTabOnly},
      {"Emulation.setGeolocationOverride", ParamFilter::kOwnTabOnly},
      {"Emulation.setLocaleOverride", ParamFilter::kOwnTabOnly},
      {"Emulation.setScrollbarsHidden", ParamFilter::kOwnTabOnly},
      {"Emulation.setTimezoneOverride", ParamFilter::kOwnTabOnly},
      {"Emulation.setTouchEmulationEnabled", ParamFilter::kOwnTabOnly},
      {"Emulation.setUserAgentOverride", ParamFilter::kOwnTabOnly},
  };
  std::map<std::string, ParamFilter> actual;
  for (const MethodRule& rule : MethodRules()) {
    if (rule.filter != ParamFilter::kNone) {
      actual[std::string(rule.method)] = rule.filter;
      // A filter only makes sense on a row that is forwarded or answered by
      // the facade.
      EXPECT_TRUE(rule.verdict == Verdict::kAllow ||
                  rule.verdict == Verdict::kFacade)
          << rule.method;
    }
  }
  EXPECT_EQ(actual, expected);
  // And Classify() hands the filter to the caller.
  EXPECT_EQ(Classify("Input.dispatchKeyEvent").filter,
            ParamFilter::kStripKeyCommands);
  EXPECT_EQ(Classify("Page.createIsolatedWorld").filter,
            ParamFilter::kIsolatedWorld);
  EXPECT_EQ(Classify("Page.enable").filter, ParamFilter::kNone);
  EXPECT_EQ(Classify("Page.noSuchMethod").filter, ParamFilter::kNone);
}

TEST(KoanAgentPolicyTest, KoanMethodsBelongToTheGatewayNotToThisTable) {
  for (std::string_view method :
       {"Koan.pair", "Koan.authenticate", "Koan.listTabs", "Koan.openTab",
        "Koan.run"}) {
    EXPECT_EQ(Classify(method).verdict, Verdict::kDeny) << method;
  }
}

TEST(KoanAgentPolicyTest, LookAlikesAndHighBitBytesAreDenied) {
  // U+0131 (dotless i), a fullwidth P, a Cyrillic a, a high-bit byte.
  for (const char* method :
       {"Page.navig\xc4\xb1te", "\xef\xbc\xb0\x61ge.navigate",
        "P\xd0\xb0ge.navigate", "Page.navigate\x80", "\xffPage.navigate"}) {
    EXPECT_EQ(Classify(method).verdict, Verdict::kDeny) << method;
  }
}

TEST(KoanAgentPolicyTest, NothingInADeniedDomainIsReachable) {
  for (std::string_view domain : DeniedDomains()) {
    for (const char* method : {"enable", "disable", "anything", ""}) {
      EXPECT_EQ(Classify(std::string(domain) + "." + method).verdict,
                Verdict::kDeny)
          << domain;
    }
  }
}

TEST(KoanAgentPolicyTest, OnlyHttpHttpsAndAboutBlankMayBeNavigatedTo) {
  for (const char* url :
       {"http://example.com/", "https://example.com/a?b=c#d",
        "HTTPS://EXAMPLE.COM/", "https://user:pw@example.com:8443/x",
        "http://127.0.0.1:9222/", "http://localhost/", "about:blank",
        "about:blank#frag", "about:blank?x"}) {
    EXPECT_TRUE(IsAllowedNavigationUrl(GURL(url))) << url;
  }
  for (const char* url :
       {"javascript:alert(1)",
        "JavaScript:alert(1)",
        "file:///etc/hosts",
        "chrome://settings",
        "chrome://version/",
        "chrome-untrusted://x/",
        "chrome-extension://abcdefghijklmnopabcdefghijklmnop/page.html",
        "view-source:https://example.com/",
        "data:text/html,<b>x</b>",
        "blob:https://example.com/0a1b",
        "filesystem:https://example.com/t/x",
        "devtools://devtools/bundled/inspector.html",
        "ws://example.com/",
        "wss://example.com/",
        "ftp://example.com/",
        "about:srcdoc",
        "about:config",
        "about:blank@evil.com",
        "isolated-app://x",
        ""}) {
    EXPECT_FALSE(IsAllowedNavigationUrl(GURL(url))) << url;
  }
}

TEST(KoanAgentPolicyTest, TheErrorNamesTheScheme) {
  EXPECT_EQ(DisallowedNavigationScheme(GURL("javascript:alert(1)")),
            "javascript");
  EXPECT_EQ(DisallowedNavigationScheme(GURL("file:///etc/hosts")), "file");
  EXPECT_EQ(DisallowedNavigationScheme(GURL("chrome://settings")), "chrome");
  EXPECT_EQ(DisallowedNavigationScheme(GURL("not a url")), "(invalid)");
  EXPECT_EQ(DisallowedNavigationScheme(GURL()), "(invalid)");
  // "(invalid)" cannot be mistaken for a scheme named invalid.
  EXPECT_EQ(DisallowedNavigationScheme(GURL("invalid:x")), "invalid");
}

TEST(KoanAgentPolicyTest, ALongSchemeIsTruncatedInTheError) {
  // Whether GURL accepts a 100000-character scheme or calls the URL invalid,
  // the error text stays short.
  const GURL long_scheme(std::string(100000, 'a') + ":payload");
  const std::string reported = DisallowedNavigationScheme(long_scheme);
  EXPECT_LE(reported.size(), 35u);
  if (long_scheme.is_valid()) {
    EXPECT_EQ(reported.substr(reported.size() - 3), "...");
  } else {
    EXPECT_EQ(reported, "(invalid)");
  }
  const GURL exact(std::string(32, 'b') + ":x");
  if (exact.is_valid()) {
    EXPECT_EQ(DisallowedNavigationScheme(exact), std::string(32, 'b'));
  }
}

}  // namespace koan::agent
