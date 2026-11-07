// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_filters.h"

#include <string>
#include <string_view>

#include "base/test/values_test_util.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_policy.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {

namespace {

base::DictValue Dict(std::string_view json) {
  return base::test::ParseJsonDict(json);
}

std::optional<FilterError> Apply(ParamFilter filter,
                                 base::DictValue& params,
                                 bool agent_owned = true,
                                 StreamRegistry* streams = nullptr) {
  FilterContext context;
  context.tab_is_agent_owned = agent_owned;
  context.streams = streams;
  return ApplyParamFilter(filter, params, context);
}

}  // namespace

TEST(KoanAgentFiltersTest, NoFilterChangesNothing) {
  base::DictValue params = Dict(R"({"a":1,"commands":["paste"]})");
  EXPECT_FALSE(Apply(ParamFilter::kNone, params).has_value());
  EXPECT_EQ(params, Dict(R"({"a":1,"commands":["paste"]})"));
}

TEST(KoanAgentFiltersTest, NavigationAcceptsHttpHttpsAndAboutBlank) {
  for (const char* url :
       {"https://example.com/", "http://localhost:3000/x?y#z", "about:blank"}) {
    base::DictValue params = Dict("{}");
    params.Set("url", std::string(url));
    const base::DictValue before = params.Clone();
    EXPECT_FALSE(Apply(ParamFilter::kNavigationUrl, params).has_value()) << url;
    EXPECT_EQ(params, before) << url;  // forwarded as it came, not rewritten
  }
}

TEST(KoanAgentFiltersTest,
     NavigationRefusesTheOthersWithAnErrorNamingTheScheme) {
  for (const char* url :
       {"javascript:alert(1)", "file:///etc/hosts", "chrome://settings",
        "view-source:https://example.com/", "data:text/html,x",
        "java\nscript:alert(1)", ""}) {
    base::DictValue params = Dict("{}");
    params.Set("url", std::string(url));
    const auto error = Apply(ParamFilter::kNavigationUrl, params);
    ASSERT_TRUE(error.has_value()) << url;
    EXPECT_EQ(error->code, "Koan.urlNotAllowed") << url;
  }
  base::DictValue params = Dict("{}");
  params.Set("url", std::string("javascript:alert(1)"));
  EXPECT_EQ(Apply(ParamFilter::kNavigationUrl, params)->message,
            "navigation to javascript is not allowed");
}

TEST(KoanAgentFiltersTest, NavigationNeedsAStringUrl) {
  for (const char* json :
       {"{}", R"({"url":null})", R"({"url":5})", R"({"url":["https://x/"]})",
        R"({"URL":"https://example.com/"})"}) {
    base::DictValue params = Dict(json);
    const auto error = Apply(ParamFilter::kNavigationUrl, params);
    ASSERT_TRUE(error.has_value()) << json;
    EXPECT_EQ(error->code, "Koan.invalidParams") << json;
  }
}

TEST(KoanAgentFiltersTest, KeyEventsLoseTheirCommands) {
  base::DictValue params = Dict(
      R"({"type":"keyDown","key":"v","modifiers":4,"commands":["paste"]})");
  EXPECT_FALSE(Apply(ParamFilter::kStripKeyCommands, params).has_value());
  EXPECT_EQ(params, Dict(R"({"type":"keyDown","key":"v","modifiers":4})"));
  // Nothing to strip is fine.
  EXPECT_FALSE(Apply(ParamFilter::kStripKeyCommands, params).has_value());
}

TEST(KoanAgentFiltersTest, IsolatedWorldsNeverGetUniversalAccess) {
  for (const char* json :
       {R"({"frameId":"F"})", R"({"frameId":"F","grantUniveralAccess":true})",
        R"({"frameId":"F","grantUniveralAccess":false})",
        R"({"frameId":"F","grantUniversalAccess":true})",
        R"({"frameId":"F","grantUniveralAccess":"yes"})"}) {
    base::DictValue params = Dict(json);
    EXPECT_FALSE(Apply(ParamFilter::kIsolatedWorld, params).has_value())
        << json;
    EXPECT_EQ(params.FindBool("grantUniveralAccess"), false) << json;
    EXPECT_FALSE(params.contains("grantUniversalAccess")) << json;
    EXPECT_EQ(*params.FindString("frameId"), "F") << json;
  }
}

TEST(KoanAgentFiltersTest, ScreenshotsNeverUseTheRealWindowSurface) {
  for (const char* json :
       {R"({"fromSurface":false})", R"({"fromSurface":0})",
        R"({"fromSurface":null})", R"({"fromSurface":"false"})"}) {
    for (bool owned : {true, false}) {
      base::DictValue params = Dict(json);
      const auto error = Apply(ParamFilter::kScreenshot, params, owned);
      ASSERT_TRUE(error.has_value()) << json;
      EXPECT_EQ(error->code, "Koan.notPermitted") << json;
    }
  }
}

TEST(KoanAgentFiltersTest, ClipAndBeyondViewportOnlyOnAgentOwnedTabs) {
  const char* kClip =
      R"({"clip":{"x":0,"y":0,"width":10,"height":10,"scale":1}})";
  const char* kBeyond = R"({"captureBeyondViewport":true})";
  for (const char* json : {kClip, kBeyond}) {
    base::DictValue owned = Dict(json);
    EXPECT_FALSE(Apply(ParamFilter::kScreenshot, owned, true).has_value())
        << json;
    base::DictValue users = Dict(json);
    const auto error = Apply(ParamFilter::kScreenshot, users, false);
    ASSERT_TRUE(error.has_value()) << json;
    EXPECT_EQ(error->code, "Koan.notPermitted") << json;
  }
  // A plain viewport screenshot is fine on the user's tab.
  for (const char* json :
       {"{}", R"({"format":"png"})", R"({"fromSurface":true})",
        R"({"captureBeyondViewport":false})"}) {
    base::DictValue params = Dict(json);
    EXPECT_FALSE(Apply(ParamFilter::kScreenshot, params, false).has_value())
        << json;
  }
}

TEST(KoanAgentFiltersTest, NetworkEnableNeverInlinesBodiesAndBoundsTheBuffers) {
  base::DictValue params = Dict(R"({
    "maxPostDataSize": 100000, "maxTotalBufferSize": 999999999,
    "maxResourceBufferSize": 5000, "reportDirectSocketTraffic": true,
    "enableDurableMessages": true})");
  EXPECT_FALSE(Apply(ParamFilter::kNetworkEnable, params).has_value());
  EXPECT_FALSE(params.contains("maxPostDataSize"));
  EXPECT_FALSE(params.contains("reportDirectSocketTraffic"));
  EXPECT_FALSE(params.contains("enableDurableMessages"));
  EXPECT_EQ(*params.FindInt("maxTotalBufferSize"), kMaxNetworkTotalBufferSize);
  EXPECT_EQ(*params.FindInt("maxResourceBufferSize"), 5000);  // already smaller

  base::DictValue empty = Dict("{}");
  EXPECT_FALSE(Apply(ParamFilter::kNetworkEnable, empty).has_value());
  EXPECT_EQ(*empty.FindInt("maxTotalBufferSize"), kMaxNetworkTotalBufferSize);
  EXPECT_EQ(*empty.FindInt("maxResourceBufferSize"),
            kMaxNetworkResourceBufferSize);

  for (const char* json :
       {R"({"maxTotalBufferSize":-1})", R"({"maxTotalBufferSize":"big"})",
        R"({"maxTotalBufferSize":1e30})"}) {
    base::DictValue odd = Dict(json);
    EXPECT_FALSE(Apply(ParamFilter::kNetworkEnable, odd).has_value()) << json;
    EXPECT_EQ(*odd.FindInt("maxTotalBufferSize"), kMaxNetworkTotalBufferSize)
        << json;
  }
}

TEST(KoanAgentFiltersTest, OnlyStreamsTheSessionCreatedCanBeRead) {
  StreamRegistry streams;
  base::DictValue unknown = Dict(R"({"handle":"1"})");
  auto error = Apply(ParamFilter::kOwnStream, unknown, true, &streams);
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code, "Koan.notPermitted");

  ASSERT_TRUE(
      streams.RecordResult("Page.printToPDF", Dict(R"({"stream":"1"})")));
  base::DictValue known = Dict(R"({"handle":"1","size":100})");
  EXPECT_FALSE(
      Apply(ParamFilter::kOwnStream, known, true, &streams).has_value());

  // Other handles, other types, no handle, and no registry at all.
  for (const char* json : {R"({"handle":"2"})", R"({"handle":1})", "{}"}) {
    base::DictValue params = Dict(json);
    EXPECT_TRUE(
        Apply(ParamFilter::kOwnStream, params, true, &streams).has_value())
        << json;
  }
  base::DictValue again = Dict(R"({"handle":"1"})");
  EXPECT_TRUE(Apply(ParamFilter::kOwnStream, again, true, nullptr).has_value());

  streams.Remove("1");
  EXPECT_TRUE(
      Apply(ParamFilter::kOwnStream, again, true, &streams).has_value());
}

TEST(KoanAgentFiltersTest, TheStreamRegistryIsBoundedAndRecordsOnlyPrintToPdf) {
  StreamRegistry streams;
  EXPECT_TRUE(streams.RecordResult("Page.captureScreenshot",
                                   Dict(R"({"stream":"x"})")));
  EXPECT_FALSE(streams.Contains("x"));
  for (size_t i = 0; i < StreamRegistry::kMaxHandles; ++i) {
    ASSERT_TRUE(streams.Add("h" + std::to_string(i)));
  }
  EXPECT_FALSE(streams.Add("one too many"));
  EXPECT_TRUE(streams.Add("h0"));  // already there
  EXPECT_FALSE(
      streams.RecordResult("Page.printToPDF", Dict(R"({"stream":"full"})")));
  // A printToPDF that returned data, not a stream.
  EXPECT_TRUE(streams.RecordResult("Page.printToPDF", Dict(R"({"data":"x"})")));
}

TEST(KoanAgentFiltersTest, EmulationIsForAgentOwnedTabsOnly) {
  base::DictValue owned = Dict("{}");
  EXPECT_FALSE(Apply(ParamFilter::kOwnTabOnly, owned, true).has_value());
  base::DictValue users = Dict("{}");
  const auto error = Apply(ParamFilter::kOwnTabOnly, users, false);
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code, "Koan.notPermitted");
}

TEST(KoanAgentFiltersTest,
     TheExecutionContextRuleRefusesUntilItsTrackerExists) {
  base::DictValue params = Dict(R"({"expression":"1"})");
  const auto error = Apply(ParamFilter::kRuntimeContext, params);
  ASSERT_TRUE(error.has_value());
  EXPECT_EQ(error->code, "Koan.unavailable");
}

}  // namespace koan::agent
