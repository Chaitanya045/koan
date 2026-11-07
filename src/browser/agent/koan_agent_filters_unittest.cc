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

TEST(KoanAgentFiltersTest, UnreviewedParametersNeverReachTheTab) {
  // The three rewriting filters keep only the parameters they were written for.
  base::DictValue key = Dict(R"({"type":"keyDown","key":"a","newThing":true,
                                "Commands":["paste"],"commands":["copy"]})");
  EXPECT_FALSE(Apply(ParamFilter::kStripKeyCommands, key).has_value());
  EXPECT_EQ(key, Dict(R"({"type":"keyDown","key":"a"})"));

  base::DictValue world = Dict(R"({"frameId":"F","worldName":"w","newPower":1,
               "contentSecurityPolicy":"default-src 'none'"})");
  EXPECT_FALSE(Apply(ParamFilter::kIsolatedWorld, world).has_value());
  EXPECT_FALSE(world.contains("newPower"));
  EXPECT_EQ(*world.FindString("worldName"), "w");
  EXPECT_TRUE(world.contains("contentSecurityPolicy"));

  base::DictValue network =
      Dict(R"({"maxTotalBufferSize":1,"newOption":true})");
  EXPECT_FALSE(Apply(ParamFilter::kNetworkEnable, network).has_value());
  EXPECT_FALSE(network.contains("newOption"));
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
  const char* kBoth =
      R"({"captureBeyondViewport":true,)"
      R"("clip":{"x":0,"y":0,"width":10,"height":10,"scale":1}})";
  for (const char* json : {kClip, kBoth}) {
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

TEST(KoanAgentFiltersTest, CaptureBeyondTheViewportNeedsAClipThatBoundsIt) {
  // Without a clip the whole page is rendered (up to 128K pixels a side).
  for (const char* json :
       {R"({"captureBeyondViewport":true})",
        R"({"captureBeyondViewport":true,"format":"png"})"}) {
    base::DictValue params = Dict(json);
    const auto error = Apply(ParamFilter::kScreenshot, params, true);
    ASSERT_TRUE(error.has_value()) << json;
    EXPECT_EQ(error->code, "Koan.notPermitted") << json;
  }
  base::DictValue not_a_bool = Dict(R"({"captureBeyondViewport":"yes"})");
  EXPECT_EQ(Apply(ParamFilter::kScreenshot, not_a_bool, true)->code,
            "Koan.invalidParams");
}

TEST(KoanAgentFiltersTest, AClipIsBoundedInEveryDirection) {
  const auto clip = [](const char* fields) {
    return std::string(R"({"clip":{)") + fields + "}}";
  };
  const char* kGood = R"("x":1,"y":2,"width":16384,"height":2000,"scale":1)";
  base::DictValue good = Dict(clip(kGood));
  EXPECT_FALSE(Apply(ParamFilter::kScreenshot, good, true).has_value());

  for (const char* bad : {
           // too big
           R"("x":0,"y":0,"width":1e9,"height":1e9,"scale":10)",
           R"("x":0,"y":0,"width":16385,"height":10,"scale":1)",
           R"("x":0,"y":0,"width":10,"height":16385,"scale":1)",
           R"("x":0,"y":0,"width":10,"height":10,"scale":4.01)",
           R"("x":1000001,"y":0,"width":10,"height":10,"scale":1)",
           // 16384 x 16384 x 1 = 268 Mpx, over the 64 Mpx cap
           R"("x":0,"y":0,"width":16384,"height":16384,"scale":1)",
           // 8000 x 8000 x 4 x 4 is over the cap although each is in range
           R"("x":0,"y":0,"width":8000,"height":8000,"scale":4)",
           // zero, negative
           R"("x":0,"y":0,"width":0,"height":10,"scale":1)",
           R"("x":0,"y":0,"width":10,"height":0,"scale":1)",
           R"("x":0,"y":0,"width":-10,"height":10,"scale":1)",
           R"("x":0,"y":0,"width":10,"height":10,"scale":0)",
           R"("x":0,"y":0,"width":10,"height":10,"scale":-1)",
           R"("x":-1,"y":0,"width":10,"height":10,"scale":1)",
           // missing, or not numbers
           R"("x":0,"y":0,"width":10,"height":10)",
           R"("y":0,"width":10,"height":10,"scale":1)",
           R"("x":"0","y":0,"width":10,"height":10,"scale":1)",
           R"("x":0,"y":0,"width":null,"height":10,"scale":1)",
           R"("x":0,"y":0,"width":true,"height":10,"scale":1)",
       }) {
    base::DictValue params = Dict(clip(bad));
    const auto error = Apply(ParamFilter::kScreenshot, params, true);
    ASSERT_TRUE(error.has_value()) << bad;
    EXPECT_EQ(error->code, "Koan.invalidParams") << bad;
  }
  // The clip itself must be an object.
  for (const char* json : {R"({"clip":"big"})", R"({"clip":[1,2,3,4,5]})",
                           R"({"clip":null})", R"({"clip":5})"}) {
    base::DictValue params = Dict(json);
    const auto error = Apply(ParamFilter::kScreenshot, params, true);
    ASSERT_TRUE(error.has_value()) << json;
    EXPECT_EQ(error->code, "Koan.invalidParams") << json;
  }
}

TEST(KoanAgentFiltersTest, FormatAndQualityAreChecked) {
  for (const char* json :
       {R"({"format":"png"})", R"({"format":"jpeg","quality":0})",
        R"({"format":"webp","quality":100})", R"({"quality":80.0})",
        R"({"quality":0.0})"}) {
    base::DictValue params = Dict(json);
    EXPECT_FALSE(Apply(ParamFilter::kScreenshot, params, false).has_value())
        << json;
  }
  for (const char* json :
       {R"({"format":"gif"})", R"({"format":"PNG"})", R"({"format":5})",
        R"({"quality":101})", R"({"quality":-1})", R"({"quality":50.5})",
        R"({"quality":"50"})"}) {
    base::DictValue params = Dict(json);
    const auto error = Apply(ParamFilter::kScreenshot, params, false);
    ASSERT_TRUE(error.has_value()) << json;
    EXPECT_EQ(error->code, "Koan.invalidParams") << json;
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
