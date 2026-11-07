// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_events.h"

#include <set>
#include <string>
#include <string_view>

#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/test/values_test_util.h"
#include "base/values.h"
#include "koan/browser/agent/koan_agent_policy.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {

namespace {

base::DictValue Dict(std::string_view json) {
  return base::test::ParseJsonDict(json);
}

std::string Json(const base::DictValue& dict) {
  return *base::WriteJson(dict);
}

// Whether `text` (JSON) mentions `needle` in any case.
bool Mentions(const base::DictValue& dict, std::string_view needle) {
  return base::ToLowerASCII(Json(dict)).find(base::ToLowerASCII(needle)) !=
         std::string::npos;
}

}  // namespace

TEST(KoanAgentEventsTest, TheTableIsSortedAndUnique) {
  std::string_view previous;
  for (const EventRule& rule : EventRules()) {
    EXPECT_LT(previous, rule.method) << rule.method;
    previous = rule.method;
  }
}

TEST(KoanAgentEventsTest, EveryNetworkEventIsScrubbedAndNoOtherDomainIsRead) {
  std::set<std::string> domains;
  for (const EventRule& rule : EventRules()) {
    const std::string_view domain =
        rule.method.substr(0, rule.method.find('.'));
    domains.insert(std::string(domain));
    EXPECT_EQ(rule.action == EventAction::kScrubNetwork, domain == "Network")
        << rule.method;
  }
  // Events of a domain the policy denies as a whole never reach the agent.
  for (std::string_view denied : DeniedDomains()) {
    EXPECT_EQ(domains.count(std::string(denied)), 0u) << denied;
  }
}

TEST(KoanAgentEventsTest, UnlistedEventsAreDropped) {
  for (std::string_view method :
       {"", "Network.dataReceived", "Network.responseReceivedEarlyHints",
        "Network.webSocketWillSendHandshakeRequest",
        "Network.eventSourceMessageReceived", "Network.requestWillBeSent ",
        "network.requestWillBeSent", "Page.downloadWillBegin",
        "Page.fileChooserOpened", "Debugger.paused", "Fetch.requestPaused",
        "Storage.cookieListItemAdded", "Target.attachedToTarget", "Koan.paused",
        "Page.frameNavigated\n", "NoSuch.event"}) {
    base::DictValue params = Dict(R"({"secret":"x"})");
    EXPECT_EQ(FilterEvent(method, params), EventVerdict::kDrop) << method;
  }
}

TEST(KoanAgentEventsTest, ListedNonNetworkEventsAreDeliveredUnchanged) {
  const char* kParams =
      R"({"frame":{"id":"F","url":"https://example.com/","cookie":"kept"},)"
      R"("postData":"kept","headers":{"Cookie":"kept"}})";
  for (std::string_view method :
       {"Page.frameNavigated", "Page.lifecycleEvent", "Log.entryAdded",
        "DOM.documentUpdated", "Inspector.detached"}) {
    base::DictValue params = Dict(kParams);
    EXPECT_EQ(FilterEvent(method, params), EventVerdict::kDeliver) << method;
    EXPECT_EQ(params, Dict(kParams)) << method;
  }
}

// An extension's content script runs in an isolated world of the tab, and with
// Runtime.enable the tab reports that world's console output and exceptions
// too. Anything tied to an execution context waits for the tracker, which lets
// through the main world and the worlds the agent created; until it exists
// nothing of the kind is delivered.
TEST(KoanAgentEventsTest, EventsTiedToAnExecutionContextWaitForTheTracker) {
  for (std::string_view method :
       {"Runtime.executionContextCreated", "Runtime.executionContextDestroyed",
        "Runtime.executionContextsCleared", "Runtime.consoleAPICalled",
        "Runtime.exceptionThrown", "Runtime.bindingCalled"}) {
    base::DictValue params = Dict(R"({"executionContextId":7})");
    EXPECT_EQ(FilterEvent(method, params), EventVerdict::kHoldForContextTracker)
        << method;
  }
}

TEST(KoanAgentEventsTest, OnlyKDeliverDelivers) {
  for (const EventRule& rule : EventRules()) {
    base::DictValue params = Dict("{}");
    const EventVerdict verdict = FilterEvent(rule.method, params);
    EXPECT_EQ(verdict == EventVerdict::kDeliver,
              rule.action != EventAction::kHoldForContextTracker)
        << rule.method;
  }
}

// Fetch.requestPaused.responseHeaders is an array of {name, value}, not a map.
TEST(KoanAgentEventsTest, HeaderEntryArraysLoseTheirSecretEntries) {
  base::DictValue params = Dict(R"({
    "responseHeaders": [{"name": "Content-Type", "value": "text/html"},
                        {"name": "Set-Cookie", "value": "sid=SECRET"},
                        {"name": "AUTHORIZATION", "value": "SECRET"},
                        {"name": "x-ok", "value": "1"}],
    "nested": {"list": [[{"name": "cookie", "value": "SECRET"}]]}
  })");
  ASSERT_TRUE(ScrubNetworkParams(params));
  EXPECT_FALSE(Mentions(params, "SECRET"));
  const base::ListValue& headers = *params.FindList("responseHeaders");
  ASSERT_EQ(headers.size(), 2u);
  EXPECT_EQ(*headers[0].GetDict().FindString("name"), "Content-Type");
  EXPECT_EQ(*headers[1].GetDict().FindString("name"), "x-ok");
}

TEST(KoanAgentEventsTest,
     RequestWillBeSentLosesCookiesAuthorizationAndTheBody) {
  base::DictValue params = Dict(R"({
    "requestId": "1", "documentURL": "https://example.com/",
    "request": {
      "url": "https://example.com/login", "method": "POST",
      "headers": {"Content-Type": "application/x-www-form-urlencoded",
                  "Cookie": "sid=SECRET", "cOoKiE": "x", "COOKIE2": "x",
                  "Authorization": "Bearer SECRET",
                  "Proxy-Authorization": "Basic SECRET", "Accept": "*/*"},
      "hasPostData": true, "postData": "password=SECRET",
      "postDataEntries": [{"bytes": "cGFzc3dvcmQ9U0VDUkVU"}]
    },
    "redirectResponse": {
      "url": "https://example.com/a", "status": 302,
      "headers": {"Set-Cookie": "sid=SECRET", "location": "/b"},
      "headersText": "HTTP/1.1 302\r\nSet-Cookie: sid=SECRET\r\n",
      "requestHeaders": {"Cookie": "sid=SECRET", "Host": "example.com"},
      "requestHeadersText": "GET /a\r\nCookie: sid=SECRET\r\n"
    },
    "type": "Document", "frameId": "F"
  })");
  ASSERT_EQ(FilterEvent("Network.requestWillBeSent", params),
            EventVerdict::kDeliver);
  EXPECT_FALSE(Mentions(params, "SECRET"));
  EXPECT_FALSE(Mentions(params, "cookie"));
  EXPECT_FALSE(Mentions(params, "authorization"));
  EXPECT_FALSE(Mentions(params, "postData"));
  // What is not secret stays.
  EXPECT_EQ(*params.FindStringByDottedPath("request.method"), "POST");
  EXPECT_EQ(*params.FindStringByDottedPath("request.headers.Accept"), "*/*");
  EXPECT_EQ(*params.FindStringByDottedPath("redirectResponse.headers.location"),
            "/b");
  EXPECT_EQ(
      *params.FindStringByDottedPath("redirectResponse.requestHeaders.Host"),
      "example.com");
  EXPECT_EQ(*params.FindString("frameId"), "F");
}

TEST(KoanAgentEventsTest, TheExtraInfoEventsLoseTheirCookies) {
  base::DictValue request_extra = Dict(R"({
    "requestId": "1",
    "associatedCookies": [{"cookie": {"name": "sid", "value": "SECRET",
                                      "httpOnly": true},
                           "blockedReasons": [], "exemptionReason": "None"}],
    "headers": {"Cookie": "sid=SECRET", "Accept": "*/*"},
    "connectTiming": {"requestTime": 1.5},
    "siteHasCookieInOtherPartition": true
  })");
  ASSERT_EQ(FilterEvent("Network.requestWillBeSentExtraInfo", request_extra),
            EventVerdict::kDeliver);
  EXPECT_FALSE(Mentions(request_extra, "SECRET"));
  EXPECT_FALSE(Mentions(request_extra, "cookie"));
  EXPECT_TRUE(request_extra.FindByDottedPath("connectTiming.requestTime"));
  EXPECT_EQ(*request_extra.FindStringByDottedPath("headers.Accept"), "*/*");

  base::DictValue response_extra = Dict(R"({
    "requestId": "1",
    "blockedCookies": [{"blockedReasons": ["SecureOnly"],
                        "cookie": {"name": "sid", "value": "SECRET"}}],
    "headers": {"Set-Cookie": "sid=SECRET; HttpOnly", "Content-Type": "text/html"},
    "headersText": "HTTP/1.1 200\r\nSet-Cookie: sid=SECRET\r\n",
    "statusCode": 200,
    "cookiePartitionKey": {"topLevelSite": "https://example.com"},
    "cookiePartitionKeyOpaque": false,
    "exemptedCookies": []
  })");
  ASSERT_EQ(FilterEvent("Network.responseReceivedExtraInfo", response_extra),
            EventVerdict::kDeliver);
  EXPECT_FALSE(Mentions(response_extra, "SECRET"));
  EXPECT_FALSE(Mentions(response_extra, "cookie"));
  EXPECT_EQ(*response_extra.FindInt("statusCode"), 200);
  EXPECT_EQ(*response_extra.FindStringByDottedPath("headers.Content-Type"),
            "text/html");
}

TEST(KoanAgentEventsTest, ResponseReceivedLosesSetCookieAndRawHeaders) {
  base::DictValue params = Dict(R"({
    "requestId": "1", "type": "Document",
    "response": {
      "url": "https://example.com/", "status": 200,
      "headers": {"set-cookie": "sid=SECRET", "Content-Length": "5"},
      "headersText": "HTTP/1.1 200\r\nSet-Cookie: sid=SECRET\r\n",
      "requestHeaders": {"Authorization": "Bearer SECRET"},
      "requestHeadersText": "GET /\r\nAuthorization: Bearer SECRET\r\n",
      "mimeType": "text/html"
    }
  })");
  ASSERT_EQ(FilterEvent("Network.responseReceived", params),
            EventVerdict::kDeliver);
  EXPECT_FALSE(Mentions(params, "SECRET"));
  EXPECT_EQ(*params.FindStringByDottedPath("response.mimeType"), "text/html");
  EXPECT_EQ(*params.FindStringByDottedPath("response.headers.Content-Length"),
            "5");
}

TEST(KoanAgentEventsTest, TheScrubberFindsSecretsWhereverTheyAreNested) {
  base::DictValue params = Dict(R"({
    "a": {"b": [{"c": {"requestHeaders": {"Cookie": "SECRET", "X": "ok"},
                        "myCookies": ["SECRET"], "x-postdata": "SECRET"}}]}
  })");
  ASSERT_EQ(FilterEvent("Network.loadingFinished", params),
            EventVerdict::kDeliver);
  EXPECT_FALSE(Mentions(params, "SECRET"));
  const base::DictValue& c =
      *(*params.FindDict("a")->FindList("b"))[0].GetDict().FindDict("c");
  EXPECT_EQ(*c.FindStringByDottedPath("requestHeaders.X"), "ok");
  EXPECT_EQ(c.size(), 1u);
}

TEST(KoanAgentEventsTest, AnEventNestedTooDeeplyIsDroppedNotDelivered) {
  base::DictValue params;
  base::DictValue* current = &params;
  for (int i = 0; i < kMaxScrubDepth + 5; ++i) {
    current = current->Set("n", base::DictValue())->GetIfDict();
  }
  current->Set("Cookie", "SECRET");
  EXPECT_EQ(FilterEvent("Network.requestWillBeSent", params),
            EventVerdict::kDrop);
}

TEST(KoanAgentEventsTest, ScrubNetworkParamsKeepsAnEmptyObjectEmpty) {
  base::DictValue params = Dict("{}");
  EXPECT_TRUE(ScrubNetworkParams(params));
  EXPECT_TRUE(params.empty());
}

}  // namespace koan::agent
