// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_facade.h"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/bind.h"
#include "base/test/simple_test_tick_clock.h"
#include "koan/browser/agent/koan_agent_redact.h"
#include "koan/browser/agent/koan_agent_registry.h"
#include "koan/browser/agent/koan_agent_secrets.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace koan::agent {
namespace {

base::DictValue Parse(std::string_view json) {
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  EXPECT_TRUE(dict) << json;
  return dict ? std::move(*dict) : base::DictValue();
}

class FakeBackend : public FacadeBackend {
 public:
  raw_ptr<const TargetRegistry> registry = nullptr;

  std::optional<TabFacts> DescribeTab(std::string_view target_id) override {
    auto it = tabs.find(std::string(target_id));
    if (it == tabs.end()) {
      return std::nullopt;
    }
    return it->second;
  }
  bool AttachTab(const std::string& target_id) override {
    if (refuse_attach || !tabs.contains(target_id)) {
      return false;
    }
    attached.insert(target_id);
    return true;
  }
  void DetachTab(const std::string& target_id) override {
    attached.erase(target_id);
  }
  void SendToTab(const std::string& target_id,
                 int command_id,
                 std::string json) override {
    sent.push_back({target_id, Parse(json)});
    EXPECT_EQ(sent.back().command.FindInt("id"), command_id);
  }
  void CommandAnswered(const std::string& target_id) override { ++answered; }
  void HoldTab(const std::string& target_id, bool hold) override {
    holds[target_id] += hold ? 1 : -1;
    EXPECT_GE(holds[target_id], 0) << target_id;
  }
  std::optional<LowPowerLimits> KeepTabAlive(
      const std::string& target_id,
      const std::string& key,
      base::TimeDelta duration) override {
    if (low_power && duration > base::Seconds(60)) {
      return LowPowerLimits{.max_leases = 2, .max_keep_alive_seconds = 60};
    }
    if (duration.is_zero()) {
      keep_alives.erase(key);
    } else {
      keep_alives[key] = {target_id, duration};
    }
    return std::nullopt;
  }
  void OpenTab(const std::string& task,
               const std::string& saved_container,
               const GURL& url,
               OpenTabCallback done) override {
    opened_urls.push_back(url);
    opened_in.push_back(task + "|" + saved_container);
    std::move(done).Run(std::nullopt, "Koan.unavailable", "no containers");
  }
  void EndTask(const std::string& task) override { ended.push_back(task); }
  bool CloseTab(const std::string& target_id) override {
    closed.push_back(target_id);
    return true;
  }
  void TakeOver(const std::optional<std::string>& target_id,
                OpenTabCallback done) override {
    take_over_asked = true;
    std::move(done).Run(std::nullopt, "Koan.denied", "no");
  }
  bool ReleaseUserTab(const std::string& target_id) override {
    released.push_back(target_id);
    return true;
  }
  // As the forwarder: the registry decides, with a site as SiteFor() makes it
  // (the test hosts are registrable domains).
  bool IsInGrantedSite(const std::string& target_id, const GURL& url) override {
    return registry &&
           registry->MayReachSite(
               target_id, url.SchemeIsHTTPOrHTTPS()
                              ? base::StrCat({url.scheme(), "://", url.host()})
                              : std::string());
  }
  void AskToResume(const std::string& target_id) override {
    resume_asked.push_back(target_id);
  }
  std::optional<SurfaceGeometry> DescribeSurface(
      const std::string& target_id) override {
    if (!tabs.contains(target_id) || !surface) {
      return std::nullopt;
    }
    return surface;
  }
  void CopySurface(const std::string& target_id,
                   SurfaceCaptureJob job,
                   SurfaceCaptureCallback done) override {
    copies.push_back({target_id, std::move(job), std::move(done)});
  }
  base::DictValue BrowserVersion() override {
    base::DictValue version;
    version.Set("product", "Koan/test");
    return version;
  }
  void Audit(AuditRecord record) override {
    audits.push_back(std::move(record));
  }
  void UserNeeded(const std::string& target_id,
                  std::string_view reason) override {
    user_needed.push_back(target_id + ":" + std::string(reason));
  }
  SecretFacts SecretsIn(const std::string& target_id,
                        const std::string& frame_id) override {
    secrets_asked.push_back(target_id + "|" + frame_id);
    SecretFacts copy;
    copy.locked = facts.locked;
    copy.focused_locked = facts.focused_locked;
    copy.card_filled = facts.card_filled;
    copy.unknown = facts.unknown;
    copy.fields = facts.fields;
    return copy;
  }
  bool SecretsCurrent(const std::string& target_id,
                      const std::string& frame_id) override {
    return secrets_current;
  }
  void RefreshSecrets(const std::string& target_id,
                      const std::string& frame_id,
                      base::OnceClosure done) override {
    refreshes.push_back(std::move(done));
  }
  void SecretsMayHaveChanged(const std::string& target_id) override {
    ++secrets_changed;
  }
  CardFields CardFieldsOf(const std::string& target_id) override {
    return card_fields;
  }
  std::optional<std::string> MaskScreenshot(
      const std::string& png,
      const ScreenshotMask& mask,
      std::string_view format,
      std::optional<int> quality) override {
    masks.push_back(mask);
    masked_format = std::string(format);
    if (refuse_mask) {
      return std::nullopt;
    }
    return "masked:" + png;
  }
  void Autofill(const std::string& target_id,
                const std::string& frame_id,
                const AutofillRequest& request,
                AutofillCallback done) override {
    autofills.push_back({target_id, frame_id, request});
    autofill_done = std::move(done);
  }
  void Upload(const std::string& target_id,
              const UploadRequest& request,
              UploadCallback done) override {
    uploads.push_back({target_id, request});
    upload_done = std::move(done);
  }
  const SecretScrubber* Secrets() override { return &scrubber; }

  struct Sent {
    std::string target_id;
    base::DictValue command;
  };
  std::map<std::string, TabFacts> tabs;
  bool refuse_attach = false;
  std::set<std::string> attached;
  std::vector<Sent> sent;
  std::vector<GURL> opened_urls;
  // "task|saved container" of each OpenTab().
  std::vector<std::string> opened_in;
  std::vector<std::string> closed;
  std::vector<std::string> user_needed;
  std::vector<std::string> ended;
  bool take_over_asked = false;
  std::vector<std::string> released;
  std::vector<std::string> resume_asked;
  std::vector<AuditRecord> audits;
  int answered = 0;
  // Screencast holds per tab, and the keep-alives by key.
  std::map<std::string, int> holds;
  struct KeptAlive {
    std::string target_id;
    base::TimeDelta duration;
  };
  std::map<std::string, KeptAlive> keep_alives;
  bool low_power = false;
  // Koan.captureScaled: the view, and the copies asked for (answered by the
  // test).
  std::optional<SurfaceGeometry> surface = SurfaceGeometry{1440, 900, 2};
  struct Copy {
    std::string target_id;
    SurfaceCaptureJob job;
    SurfaceCaptureCallback done;
  };
  std::vector<Copy> copies;
  // Secrets Koan filled (design section 4.5).
  SecretFacts facts;
  std::vector<std::string> secrets_asked;
  // The fields' read (RefreshSecrets()), answered by the test.
  bool secrets_current = true;
  std::vector<base::OnceClosure> refreshes;
  int secrets_changed = 0;
  CardFields card_fields;
  std::vector<ScreenshotMask> masks;
  std::string masked_format;
  bool refuse_mask = false;
  struct AutofillCall {
    std::string target_id;
    std::string frame_id;
    AutofillRequest request;
  };
  std::vector<AutofillCall> autofills;
  AutofillCallback autofill_done;
  struct UploadCall {
    std::string target_id;
    UploadRequest request;
  };
  std::vector<UploadCall> uploads;
  UploadCallback upload_done;
  SecretScrubber scrubber;
};

class KoanAgentFacadeTest : public testing::Test {
 protected:
  KoanAgentFacadeTest()
      : facade_(7,
                "alice",
                &registry_,
                &backend_,
                &clock_,
                base::BindRepeating(&KoanAgentFacadeTest::OnSend,
                                    base::Unretained(this))) {
    backend_.tabs["T1"] = TabFacts{"Mine", GURL("https://example.com/a")};
    backend_.tabs["T2"] = TabFacts{"Bob's", GURL("https://bank.example/")};
    backend_.tabs["U1"] = TabFacts{"User's", GURL("https://mail.example/")};
    registry_.AddAgentTab("T1", "alice", "task");
    registry_.AddAgentTab("T2", "bob", "task");
    backend_.registry = &registry_;
  }

  void OnSend(std::string json) { received_.push_back(Parse(json)); }

  void Send(int id,
            std::string method,
            base::DictValue params = base::DictValue(),
            std::optional<std::string> session_id = std::nullopt,
            bool drive = false) {
    AgentMessage message;
    message.capabilities.drive = drive;
    message.id = id;
    message.method = std::move(method);
    message.params = std::move(params);
    message.session_id = std::move(session_id);
    message.client_id = "alice";
    facade_.OnClientMessage(std::move(message));
  }

  // Attaches to `target` and returns the session id.
  std::string AttachTo(const std::string& target) {
    base::DictValue params;
    params.Set("targetId", target);
    params.Set("flatten", true);
    Send(900, "Target.attachToTarget", std::move(params));
    const base::DictValue* answer = Answer(900);
    const std::string* id =
        answer ? answer->FindStringByDottedPath("result.sessionId") : nullptr;
    return id ? *id : std::string();
  }

  // The last message with this id.
  const base::DictValue* Answer(int id) const {
    for (auto it = received_.rbegin(); it != received_.rend(); ++it) {
      if (it->FindInt("id") == id) {
        return &*it;
      }
    }
    return nullptr;
  }
  std::string ErrorCode(int id) const {
    const base::DictValue* answer = Answer(id);
    const std::string* code =
        answer ? answer->FindStringByDottedPath("error.data") : nullptr;
    return code ? *code : std::string();
  }
  std::vector<const base::DictValue*> Events(std::string_view method) const {
    std::vector<const base::DictValue*> events;
    for (const base::DictValue& message : received_) {
      const std::string* name = message.FindString("method");
      if (name && *name == method) {
        events.push_back(&message);
      }
    }
    return events;
  }

  // The tab answers the last command it was sent.
  void AnswerLast(base::DictValue result) {
    ASSERT_FALSE(backend_.sent.empty());
    base::DictValue answer;
    answer.Set("id", *backend_.sent.back().command.FindInt("id"));
    answer.Set("result", std::move(result));
    facade_.OnTabMessage(backend_.sent.back().target_id,
                         *base::WriteJson(answer));
  }
  void TabEvent(const std::string& target,
                std::string_view method,
                base::DictValue params) {
    base::DictValue event;
    event.Set("method", method);
    event.Set("params", std::move(params));
    facade_.OnTabMessage(target, *base::WriteJson(event));
  }

  base::SimpleTestTickClock clock_;
  TargetRegistry registry_;
  FakeBackend backend_;
  std::vector<base::DictValue> received_;
  CdpFacade facade_;
};

TEST_F(KoanAgentFacadeTest, ListsAndAttachesOnlyTheClientsOwnTabs) {
  Send(1, "Target.getTargets");
  const base::ListValue* infos =
      Answer(1)->FindListByDottedPath("result.targetInfos");
  ASSERT_TRUE(infos);
  ASSERT_EQ(infos->size(), 1u);
  EXPECT_EQ(*(*infos)[0].GetDict().FindString("targetId"), "T1");

  // Bob's tab and an unknown id are not there, even by a guessed id.
  for (const char* target : {"T2", "U1", "nope"}) {
    base::DictValue params;
    params.Set("targetId", target);
    params.Set("flatten", true);
    Send(2, "Target.attachToTarget", std::move(params));
    EXPECT_EQ(ErrorCode(2), "Koan.unknownTarget") << target;
  }
  EXPECT_TRUE(backend_.attached.empty());

  EXPECT_FALSE(AttachTo("T1").empty());
  EXPECT_TRUE(backend_.attached.contains("T1"));
  EXPECT_EQ(Events("Target.attachedToTarget").size(), 1u);
}

TEST_F(KoanAgentFacadeTest, AnAgentHasNoBrowserTarget) {
  Send(1, "Page.navigate", base::DictValue().Set("url", "https://a.example/"));
  EXPECT_EQ(ErrorCode(1), "Koan.notPermitted");
  Send(2, "Browser.close");
  EXPECT_EQ(ErrorCode(2), "Koan.notPermitted");
  Send(3, "Target.createBrowserContext");
  EXPECT_EQ(ErrorCode(3), "Koan.notPermitted");
  EXPECT_TRUE(backend_.sent.empty());
  // A liveness check works.
  Send(4, "Browser.getVersion");
  EXPECT_EQ(*Answer(4)->FindStringByDottedPath("result.product"), "Koan/test");
}

TEST_F(KoanAgentFacadeTest, ForwardsTheFilteredCommandAndAnswersWithTheId) {
  const std::string session = AttachTo("T1");
  base::DictValue key;
  key.Set("type", "keyDown");
  key.Set("key", "v");
  key.Set("commands", base::ListValue().Append("paste"));
  Send(41, "Input.dispatchKeyEvent", std::move(key), session);

  ASSERT_EQ(backend_.sent.size(), 1u);
  const base::DictValue& sent = backend_.sent[0].command;
  EXPECT_EQ(backend_.sent[0].target_id, "T1");
  // The facade's own id, and no clipboard command (T17).
  EXPECT_NE(sent.FindInt("id"), 41);
  EXPECT_FALSE(sent.FindDict("params")->contains("commands"));
  EXPECT_FALSE(Answer(41));

  clock_.Advance(base::Milliseconds(5));
  AnswerLast(base::DictValue());
  const base::DictValue* answer = Answer(41);
  ASSERT_TRUE(answer);
  EXPECT_EQ(*answer->FindString("sessionId"), session);
  EXPECT_TRUE(answer->FindDict("result"));
  EXPECT_EQ(backend_.answered, 1);
  ASSERT_FALSE(backend_.audits.empty());
  EXPECT_EQ(backend_.audits.back().event, "Input.dispatchKeyEvent");
  EXPECT_EQ(backend_.audits.back().outcome, AuditOutcome::kOk);
  EXPECT_EQ(backend_.audits.back().duration, base::Milliseconds(5));
  EXPECT_EQ(backend_.audits.back().tab_origin, "https://example.com");
}

TEST_F(KoanAgentFacadeTest, DeniesAndAuditsWhatThePolicyRefuses) {
  const std::string session = AttachTo("T1");
  Send(1, "Network.getAllCookies", base::DictValue(), session);
  EXPECT_EQ(ErrorCode(1), "Koan.notPermitted");
  Send(2, "Network.getResponseBody", base::DictValue().Set("requestId", "1"),
       session);
  EXPECT_EQ(ErrorCode(2), "Koan.notPermitted");
  Send(3, "Page.navigate", base::DictValue().Set("url", "file:///etc/hosts"),
       session);
  EXPECT_EQ(ErrorCode(3), "Koan.urlNotAllowed");
  EXPECT_TRUE(backend_.sent.empty());
  ASSERT_GE(backend_.audits.size(), 3u);
  EXPECT_EQ(backend_.audits.back().outcome, AuditOutcome::kDenied);
  // The audit carries the URL's origin only, never the path (T20).
  EXPECT_EQ(backend_.audits.back().summary.find("hosts"), std::string::npos);

  // No-ops succeed and are not sent.
  Send(4, "Page.bringToFront", base::DictValue(), session);
  EXPECT_TRUE(Answer(4)->FindDict("result"));
  EXPECT_TRUE(backend_.sent.empty());
}

TEST_F(KoanAgentFacadeTest, AUserTabTheAgentMayOnlyReadTakesNoInput) {
  registry_.GrantUserTab("U1", "alice", TabState::kReadable);
  const std::string session = AttachTo("U1");
  ASSERT_FALSE(session.empty());
  Send(1, "Runtime.evaluate", base::DictValue().Set("expression", "1"),
       session);
  EXPECT_EQ(ErrorCode(1), "Koan.notPermitted");
  Send(2, "Page.captureScreenshot", base::DictValue(), session);
  EXPECT_EQ(backend_.sent.size(), 1u);

  // Input on a read-only tab is refused as not permitted.
  Send(3, "Input.insertText", base::DictValue().Set("text", "hi"), session);
  EXPECT_EQ(ErrorCode(3), "Koan.notPermitted");
  EXPECT_EQ(backend_.sent.size(), 1u);

  // Pause() only pauses a driven tab. Once the user lets the agent drive it,
  // pausing refuses input as paused.
  registry_.GrantUserTab("U1", "alice", TabState::kDriven);
  ASSERT_TRUE(registry_.Pause("U1"));
  Send(4, "Input.insertText", base::DictValue().Set("text", "hi"), session);
  EXPECT_EQ(ErrorCode(4), "Koan.paused");
  EXPECT_EQ(backend_.sent.size(), 1u);
}

TEST_F(KoanAgentFacadeTest, EventsPassTheAllowlistTheScrubberAndTheTracker) {
  const std::string session = AttachTo("T1");

  base::DictValue request;
  request.SetByDottedPath("request.url", "https://example.com/login");
  request.SetByDottedPath("request.headers.Cookie", "sid=secret");
  request.SetByDottedPath("request.postData", "password=hunter2");
  TabEvent("T1", "Network.requestWillBeSent", std::move(request));
  ASSERT_EQ(Events("Network.requestWillBeSent").size(), 1u);
  const std::string delivered =
      *base::WriteJson(*Events("Network.requestWillBeSent")[0]);
  EXPECT_EQ(delivered.find("secret"), std::string::npos);
  EXPECT_EQ(delivered.find("hunter2"), std::string::npos);
  EXPECT_EQ(*Events("Network.requestWillBeSent")[0]->FindString("sessionId"),
            session);

  // Not on the allowlist.
  TabEvent("T1", "Storage.cookiesChanged", base::DictValue());
  EXPECT_TRUE(Events("Storage.cookiesChanged").empty());

  // An extension's world is never seen; the main world is.
  auto context = [](int id, bool is_default, const char* type) {
    base::DictValue params;
    params.SetByDottedPath("context.id", id);
    params.SetByDottedPath("context.uniqueId", "u" + base::NumberToString(id));
    params.SetByDottedPath("context.auxData.isDefault", is_default);
    params.SetByDottedPath("context.auxData.type", type);
    return params;
  };
  TabEvent("T1", "Runtime.executionContextCreated",
           context(1, true, "default"));
  TabEvent("T1", "Runtime.executionContextCreated",
           context(2, false, "isolated"));
  ASSERT_EQ(Events("Runtime.executionContextCreated").size(), 1u);

  // And a command may not name it.
  Send(5, "Runtime.evaluate",
       base::DictValue().Set("expression", "1").Set("contextId", 2), session);
  EXPECT_EQ(ErrorCode(5), "Koan.contextNotAllowed");

  // The world the agent makes is reported before its answer, and is released
  // by it, after the answer.
  Send(6, "Page.createIsolatedWorld",
       base::DictValue().Set("frameId", "F").Set("grantUniveralAccess", true),
       session);
  ASSERT_EQ(backend_.sent.size(), 1u);
  EXPECT_EQ(backend_.sent[0].command.FindBoolByDottedPath(
                "params.grantUniveralAccess"),
            false);
  TabEvent("T1", "Runtime.executionContextCreated",
           context(3, false, "isolated"));
  EXPECT_EQ(Events("Runtime.executionContextCreated").size(), 1u);
  AnswerLast(base::DictValue().Set("executionContextId", 3));
  EXPECT_TRUE(Answer(6));
  EXPECT_EQ(Events("Runtime.executionContextCreated").size(), 2u);
  Send(7, "Runtime.evaluate",
       base::DictValue().Set("expression", "1").Set("contextId", 3), session);
  EXPECT_EQ(backend_.sent.size(), 2u);
}

TEST_F(KoanAgentFacadeTest, DropsMessagesOfUnknownChildSessionsAndAnswers) {
  AttachTo("T1");
  base::DictValue child;
  child.Set("method", "Page.frameNavigated");
  child.Set("params", base::DictValue());
  child.Set("sessionId", "CHILD");
  facade_.OnTabMessage("T1", *base::WriteJson(child));
  facade_.OnTabMessage("T1", R"({"id": 12345, "result": {}})");
  facade_.OnTabMessage("T1", "not json");
  EXPECT_TRUE(Events("Page.frameNavigated").empty());
  EXPECT_FALSE(Answer(12345));
}

TEST_F(KoanAgentFacadeTest, ATabThatGoesAwayAnswersWhatWasInFlight) {
  const std::string session = AttachTo("T1");
  Send(8, "Page.reload", base::DictValue(), session);
  facade_.OnTabDetached("T1");
  EXPECT_EQ(ErrorCode(8), "Koan.targetClosed");
  ASSERT_EQ(Events("Target.detachedFromTarget").size(), 1u);
  EXPECT_FALSE(backend_.attached.contains("T1"));
  // The session is gone.
  Send(9, "Page.reload", base::DictValue(), session);
  EXPECT_EQ(ErrorCode(9), "Koan.unknownSession");
}

TEST_F(KoanAgentFacadeTest, OpeningATabChecksTheUrlFirst) {
  Send(1, "Koan.openTab", base::DictValue().Set("url", "javascript:alert(1)"));
  EXPECT_EQ(ErrorCode(1), "Koan.urlNotAllowed");
  EXPECT_TRUE(backend_.opened_urls.empty());
  // With no agent containers, a clear error.
  Send(2, "Target.createTarget",
       base::DictValue().Set("url", "https://example.com/"));
  EXPECT_EQ(ErrorCode(2), "Koan.unavailable");
  ASSERT_EQ(backend_.opened_urls.size(), 1u);
  Send(3, "Target.createTarget",
       base::DictValue().Set("url", "about:blank").Set("newWindow", true));
  EXPECT_EQ(ErrorCode(3), "Koan.notPermitted");
}

TEST_F(KoanAgentFacadeTest, ASavedContainerIsAskedForByName) {
  Send(1, "Koan.openTab",
       base::DictValue().Set("savedContainer", "GitHub bot"));
  Send(2, "Koan.openTab",
       base::DictValue().Set("savedContainer", "GitHub bot").Set("task", "t"));
  Send(3, "Target.createTarget",
       base::DictValue().Set("url", "about:blank").Set("savedContainer", "x"));
  Send(4, "Koan.openTab");
  EXPECT_EQ(
      backend_.opened_in,
      (std::vector<std::string>{"GitHub bot|GitHub bot", "t|GitHub bot", "x|x",
                                std::string(CdpFacade::kDefaultTask) + "|"}));
  // Not a name: refused before anything opens.
  Send(5, "Koan.openTab", base::DictValue().Set("savedContainer", 5));
  EXPECT_EQ(ErrorCode(5), "Koan.invalidParams");
  Send(6, "Koan.openTab", base::DictValue().Set("savedContainer", ""));
  EXPECT_EQ(ErrorCode(6), "Koan.invalidParams");
  EXPECT_EQ(backend_.opened_in.size(), 4u);
}

TEST_F(KoanAgentFacadeTest, EndingATaskDefaultsToTheThrowawayTask) {
  Send(1, "Koan.endTask");
  Send(2, "Koan.endTask", base::DictValue().Set("task", "research"));
  EXPECT_EQ(backend_.ended,
            (std::vector<std::string>{CdpFacade::kDefaultTask, "research"}));
  EXPECT_TRUE(Answer(2)->FindDict("result"));
}

TEST_F(KoanAgentFacadeTest, NothingButReadsReachASecurityInterstitial) {
  const std::string session = AttachTo("T1");
  // A security interstitial commits under the original https URL, so the
  // session stays attached; the facade refuses to act on it.
  backend_.tabs["T1"].interstitial = true;
  Send(1, "Input.dispatchMouseEvent",
       base::DictValue().Set("type", "mousePressed").Set("x", 5).Set("y", 5),
       session);
  EXPECT_EQ(ErrorCode(1), "Koan.interstitial");
  Send(2, "Runtime.evaluate", base::DictValue().Set("expression", "proceed()"),
       session);
  EXPECT_EQ(ErrorCode(2), "Koan.interstitial");
  EXPECT_TRUE(backend_.sent.empty());
  // The user is needed, once per interstitial.
  ASSERT_EQ(Events("Koan.userNeeded").size(), 1u);
  EXPECT_EQ(
      *Events("Koan.userNeeded")[0]->FindStringByDottedPath("params.reason"),
      "interstitial");
  EXPECT_EQ(backend_.user_needed, std::vector<std::string>{"T1:interstitial"});
  EXPECT_EQ(backend_.audits.back().outcome, AuditOutcome::kDenied);
  // Reads still work.
  Send(3, "Page.captureScreenshot", base::DictValue(), session);
  EXPECT_EQ(backend_.sent.size(), 1u);
  // Once the user has decided, the agent may act again.
  backend_.tabs["T1"].interstitial = false;
  Send(4, "Input.dispatchMouseEvent",
       base::DictValue().Set("type", "mousePressed").Set("x", 5).Set("y", 5),
       session);
  EXPECT_EQ(backend_.sent.size(), 2u);
}

TEST_F(KoanAgentFacadeTest, AttachingToATabBehindAnInterstitialSaysSo) {
  // Content refuses to attach to a tab that already shows the warning (the
  // first act on a tab opened onto a bad certificate): the answer is the
  // one acting on it gets, and the user is needed, once.
  backend_.tabs["T1"].interstitial = true;
  backend_.refuse_attach = true;
  for (int id : {1, 2}) {
    Send(id, "Target.attachToTarget",
         base::DictValue().Set("targetId", "T1").Set("flatten", true));
    EXPECT_EQ(ErrorCode(id), "Koan.interstitial");
  }
  EXPECT_EQ(backend_.user_needed, std::vector<std::string>{"T1:interstitial"});
  ASSERT_EQ(Events("Koan.userNeeded").size(), 1u);
  // Any other refusal is still Koan.unavailable.
  backend_.tabs["T1"].interstitial = false;
  Send(3, "Target.attachToTarget",
       base::DictValue().Set("targetId", "T1").Set("flatten", true));
  EXPECT_EQ(ErrorCode(3), "Koan.unavailable");
}

TEST_F(KoanAgentFacadeTest, TellsOnlyItsOwnClientWhatAPageWaitsToShow) {
  // Design section 9.4: a page's dialog in the client's tab waits for the
  // user; the client hears that it waits and when it no longer does.
  facade_.OnUserNeeded("T1", "dialog", /*needed=*/true);
  facade_.OnUserNeeded("T1", "dialog", /*needed=*/false);
  // Not another client's tab.
  facade_.OnUserNeeded("T2", "print", /*needed=*/true);
  ASSERT_EQ(Events("Koan.userNeeded").size(), 1u);
  EXPECT_EQ(
      *Events("Koan.userNeeded")[0]->FindStringByDottedPath("params.targetId"),
      "T1");
  EXPECT_EQ(
      *Events("Koan.userNeeded")[0]->FindStringByDottedPath("params.reason"),
      "dialog");
  ASSERT_EQ(Events("Koan.userNeededDone").size(), 1u);
}

// A message from Chromium's session `child` of the tab T1.
void ChildMessage(CdpFacade& facade,
                  const std::string& child,
                  base::DictValue message) {
  message.Set("sessionId", child);
  facade.OnTabMessage("T1", *base::WriteJson(message));
}

base::DictValue ChildInfo(std::string_view id,
                          std::string_view type,
                          std::string_view url) {
  base::DictValue info;
  info.Set("targetId", id);
  info.Set("type", type);
  info.Set("url", url);
  info.Set("browserContextId", "CTX");
  info.Set("openerId", "OPENER");
  return info;
}

TEST_F(KoanAgentFacadeTest, AutoAttachInATabIsTheTabsOwnWithFlatSessions) {
  const std::string session = AttachTo("T1");
  Send(1, "Target.setAutoAttach",
       base::DictValue()
           .Set("autoAttach", true)
           .Set("waitForDebuggerOnStart", true)
           .Set("flatten", true)
           .Set("filter", base::ListValue()),
       session);
  // Sent on to the tab, without the agent's filter.
  ASSERT_EQ(backend_.sent.size(), 1u);
  EXPECT_EQ(*backend_.sent[0].command.FindString("method"),
            "Target.setAutoAttach");
  EXPECT_FALSE(backend_.sent[0].command.FindDictByDottedPath("params.filter"));
  EXPECT_EQ(backend_.sent[0].command.FindBoolByDottedPath("params.flatten"),
            true);
  EXPECT_FALSE(backend_.sent[0].command.contains("sessionId"));
  AnswerLast(base::DictValue());
  EXPECT_TRUE(Answer(1)->FindDict("result"));

  // Not flat: refused before the tab sees it.
  Send(2, "Target.setAutoAttach",
       base::DictValue().Set("autoAttach", true).Set("flatten", false),
       session);
  EXPECT_EQ(ErrorCode(2), "Koan.invalidParams");
  EXPECT_EQ(backend_.sent.size(), 1u);
}

TEST_F(KoanAgentFacadeTest, AReachableFrameBecomesAChildSession) {
  const std::string session = AttachTo("T1");
  TabEvent(
      "T1", "Target.attachedToTarget",
      base::DictValue()
          .Set("sessionId", "CR1")
          .Set("targetInfo", ChildInfo("F1", "iframe", "https://pay.example/"))
          .Set("waitingForDebugger", true));
  std::vector<const base::DictValue*> attached =
      Events("Target.attachedToTarget");
  ASSERT_EQ(attached.size(), 2u);
  const base::DictValue& event = *attached.back();
  // In the tab's session, with an id of the facade's own, and nothing of the
  // browser context or opener.
  EXPECT_EQ(*event.FindString("sessionId"), session);
  const std::string child = *event.FindStringByDottedPath("params.sessionId");
  EXPECT_NE(child, "CR1");
  EXPECT_EQ(*event.FindStringByDottedPath("params.targetInfo.targetId"), "F1");
  EXPECT_FALSE(
      event.FindStringByDottedPath("params.targetInfo.browserContextId"));
  EXPECT_FALSE(event.FindStringByDottedPath("params.targetInfo.openerId"));
  EXPECT_EQ(event.FindBoolByDottedPath("params.waitingForDebugger"), true);

  // A command in the child goes in Chromium's session for it, through the
  // same policy as the tab's.
  Send(3, "Runtime.runIfWaitingForDebugger", base::DictValue(), child);
  ASSERT_EQ(backend_.sent.size(), 1u);
  EXPECT_EQ(*backend_.sent[0].command.FindString("sessionId"), "CR1");
  Send(4, "Browser.close", base::DictValue(), child);
  EXPECT_EQ(ErrorCode(4), "Koan.notPermitted");
  ASSERT_EQ(backend_.sent.size(), 1u);
  // Its answer comes back in the child's session.
  ChildMessage(facade_, "CR1",
               base::DictValue()
                   .Set("id", *backend_.sent[0].command.FindInt("id"))
                   .Set("result", base::DictValue()));
  ASSERT_TRUE(Answer(3));
  EXPECT_EQ(*Answer(3)->FindString("sessionId"), child);
  // And its events too.
  ChildMessage(facade_, "CR1",
               base::DictValue()
                   .Set("method", "Page.frameNavigated")
                   .Set("params", base::DictValue()));
  ASSERT_FALSE(Events("Page.frameNavigated").empty());
  EXPECT_EQ(*Events("Page.frameNavigated").back()->FindString("sessionId"),
            child);

  // Chromium detaching it ends it for the client.
  TabEvent("T1", "Target.detachedFromTarget",
           base::DictValue().Set("sessionId", "CR1"));
  ASSERT_EQ(Events("Target.detachedFromTarget").size(), 1u);
  EXPECT_EQ(*Events("Target.detachedFromTarget")[0]->FindStringByDottedPath(
                "params.sessionId"),
            child);
  Send(5, "Runtime.evaluate", base::DictValue().Set("expression", "1"), child);
  EXPECT_EQ(ErrorCode(5), "Koan.unknownSession");
  // The tab stays attached.
  EXPECT_TRUE(backend_.attached.contains("T1"));
}

TEST_F(KoanAgentFacadeTest, ChildrenTheAgentMayNotReachAreDetached) {
  AttachTo("T1");
  const struct {
    const char* session;
    const char* type;
    const char* url;
  } kRefused[] = {
      {"CR1", "iframe", "chrome://settings/"},
      {"CR2", "iframe", "chrome-extension://abc/page.html"},
      {"CR3", "shared_worker", "https://a.example/w.js"},
      {"CR4", "service_worker", "https://a.example/sw.js"},
      {"CR5", "page", "https://a.example/"},
  };
  for (const auto& child : kRefused) {
    TabEvent("T1", "Target.attachedToTarget",
             base::DictValue()
                 .Set("sessionId", child.session)
                 .Set("targetInfo", ChildInfo("X", child.type, child.url)));
  }
  // The client hears of none, and the tab is told to let each go.
  EXPECT_EQ(Events("Target.attachedToTarget").size(), 1u);
  ASSERT_EQ(backend_.sent.size(), std::size(kRefused));
  for (size_t i = 0; i < std::size(kRefused); ++i) {
    EXPECT_EQ(*backend_.sent[i].command.FindString("method"),
              "Target.detachFromTarget");
    EXPECT_EQ(
        *backend_.sent[i].command.FindStringByDottedPath("params.sessionId"),
        base::span(kRefused)[i].session);
  }
  // Messages of a child the facade did not accept go nowhere.
  const size_t received = received_.size();
  ChildMessage(facade_, "CR1",
               base::DictValue()
                   .Set("method", "Page.frameNavigated")
                   .Set("params", base::DictValue()));
  EXPECT_EQ(received_.size(), received);
}

TEST_F(KoanAgentFacadeTest, ChildrenGoWithTheirTabAndWithTheClient) {
  const std::string session = AttachTo("T1");
  TabEvent("T1", "Target.attachedToTarget",
           base::DictValue()
               .Set("sessionId", "CR1")
               .Set("targetInfo",
                    ChildInfo("W1", "worker", "https://example.com/w.js")));
  const std::string child = *Events("Target.attachedToTarget")
                                 .back()
                                 ->FindStringByDottedPath("params.sessionId");
  // A command in flight in the child is answered when the tab goes, and the
  // tab's command count is given back.
  Send(1, "Runtime.evaluate", base::DictValue().Set("expression", "1"), child);
  ASSERT_EQ(backend_.sent.size(), 1u);
  const int answered = backend_.answered;
  facade_.OnTabDetached("T1");
  EXPECT_EQ(ErrorCode(1), "Koan.targetClosed");
  EXPECT_EQ(backend_.answered, answered + 1);
  // Both the child and the tab are detached for the client.
  EXPECT_EQ(Events("Target.detachedFromTarget").size(), 2u);
  // The agent detaching a child tells Chromium in the parent's session.
  const std::string again = AttachTo("T1");
  TabEvent("T1", "Target.attachedToTarget",
           base::DictValue()
               .Set("sessionId", "CR2")
               .Set("targetInfo",
                    ChildInfo("F2", "iframe", "https://example.com/f")));
  const std::string second = *Events("Target.attachedToTarget")
                                  .back()
                                  ->FindStringByDottedPath("params.sessionId");
  Send(2, "Target.detachFromTarget", base::DictValue().Set("sessionId", second),
       again);
  EXPECT_TRUE(Answer(2)->FindDict("result"));
  EXPECT_EQ(*backend_.sent.back().command.FindString("method"),
            "Target.detachFromTarget");
  EXPECT_EQ(
      *backend_.sent.back().command.FindStringByDottedPath("params.sessionId"),
      "CR2");
  EXPECT_FALSE(backend_.sent.back().command.contains("sessionId"));
}

TEST_F(KoanAgentFacadeTest, AUserTabsChildrenStayOnTheGrantedSite) {
  // "Limit a taken-over tab to its site" on: the user gave the agent U1 for
  // mail.example only, so a cross-site widget in it is not the agent's.
  registry_.GrantUserTab("U1", "alice", TabState::kDriven,
                         "https://mail.example");
  const std::string session = AttachTo("U1");
  ASSERT_FALSE(session.empty());
  auto attach = [&](const char* chromium_session, const char* target,
                    const char* url) {
    facade_.OnTabMessage(
        "U1",
        *base::WriteJson(
            base::DictValue()
                .Set("method", "Target.attachedToTarget")
                .Set("params", base::DictValue()
                                   .Set("sessionId", chromium_session)
                                   .Set("targetInfo",
                                        ChildInfo(target, "iframe", url)))));
  };
  attach("CR1", "F1", "https://pay.other.example/widget");
  attach("CR2", "F2", "https://mail.example/frame");
  // Only the same-site frame is the client's; the other is let go.
  ASSERT_EQ(Events("Target.attachedToTarget").size(), 2u);
  EXPECT_EQ(*Events("Target.attachedToTarget")
                 .back()
                 ->FindStringByDottedPath("params.targetInfo.targetId"),
            "F2");
  ASSERT_EQ(backend_.sent.size(), 1u);
  EXPECT_EQ(*backend_.sent[0].command.FindString("method"),
            "Target.detachFromTarget");
  EXPECT_EQ(
      *backend_.sent[0].command.FindStringByDottedPath("params.sessionId"),
      "CR1");

  // The same-site frame navigating to another site goes too.
  facade_.OnTabMessage(
      "U1", *base::WriteJson(
                base::DictValue()
                    .Set("method", "Target.targetInfoChanged")
                    .Set("params", base::DictValue().Set(
                                       "targetInfo",
                                       ChildInfo("F2", "iframe",
                                                 "https://other.example/")))));
  ASSERT_EQ(Events("Target.detachedFromTarget").size(), 1u);
  EXPECT_EQ(
      *backend_.sent.back().command.FindStringByDottedPath("params.sessionId"),
      "CR2");
  EXPECT_TRUE(Events("Target.targetInfoChanged").empty());

  // An agent's own tab keeps a cross-site child.
  AttachTo("T1");
  TabEvent("T1", "Target.attachedToTarget",
           base::DictValue()
               .Set("sessionId", "CR3")
               .Set("targetInfo",
                    ChildInfo("F3", "iframe", "https://pay.other.example/")));
  EXPECT_EQ(*Events("Target.attachedToTarget")
                 .back()
                 ->FindStringByDottedPath("params.targetInfo.targetId"),
            "F3");
}

// The default (the limit off): a taken-over tab's frames and workers of any
// site are the agent's, but the reach rules still apply.
TEST_F(KoanAgentFacadeTest, AUserTabsChildrenOfAnySiteWithoutTheLimit) {
  registry_.GrantUserTab("U1", "alice", TabState::kDriven,
                         "https://mail.example", /*any_site=*/true);
  const std::string session = AttachTo("U1");
  ASSERT_FALSE(session.empty());
  TabEvent("U1", "Target.attachedToTarget",
           base::DictValue()
               .Set("sessionId", "CR1")
               .Set("targetInfo",
                    ChildInfo("F1", "iframe", "https://pay.other.example/w")));
  ASSERT_EQ(Events("Target.attachedToTarget").size(), 2u);
  EXPECT_EQ(*Events("Target.attachedToTarget")
                 .back()
                 ->FindStringByDottedPath("params.targetInfo.targetId"),
            "F1");
  EXPECT_TRUE(backend_.sent.empty());

  // Still not a privileged page.
  TabEvent(
      "U1", "Target.attachedToTarget",
      base::DictValue()
          .Set("sessionId", "CR2")
          .Set("targetInfo", ChildInfo("F2", "iframe", "chrome://settings/")));
  EXPECT_EQ(Events("Target.attachedToTarget").size(), 2u);
  ASSERT_EQ(backend_.sent.size(), 1u);
  EXPECT_EQ(
      *backend_.sent[0].command.FindStringByDottedPath("params.sessionId"),
      "CR2");

  // A child that moves to another site stays.
  TabEvent(
      "U1", "Target.targetInfoChanged",
      base::DictValue().Set(
          "targetInfo", ChildInfo("F1", "iframe", "https://third.example/")));
  EXPECT_TRUE(Events("Target.detachedFromTarget").empty());
  EXPECT_EQ(Events("Target.targetInfoChanged").size(), 1u);
}

TEST_F(KoanAgentFacadeTest, ClosesOnlyItsOwnAgentTabs) {
  registry_.GrantUserTab("U1", "alice", TabState::kDriven);
  Send(1, "Target.closeTarget", base::DictValue().Set("targetId", "U1"));
  EXPECT_EQ(ErrorCode(1), "Koan.notPermitted");
  Send(2, "Target.closeTarget", base::DictValue().Set("targetId", "T2"));
  EXPECT_EQ(ErrorCode(2), "Koan.unknownTarget");
  Send(3, "Target.closeTarget", base::DictValue().Set("targetId", "T1"));
  EXPECT_EQ(backend_.closed, std::vector<std::string>{"T1"});
}

TEST_F(KoanAgentFacadeTest, AutoAttachAttachesNewTargetsInScopeOnly) {
  Send(1, "Target.setAutoAttach",
       base::DictValue().Set("autoAttach", true).Set("flatten", true));
  EXPECT_EQ(Events("Target.attachedToTarget").size(), 1u);
  registry_.AddAgentTab("T3", "bob", "task");
  backend_.tabs["T3"] = TabFacts{"", GURL("about:blank")};
  facade_.OnTargetAdded("T3");
  EXPECT_EQ(Events("Target.attachedToTarget").size(), 1u);
  registry_.AddAgentTab("T4", "alice", "task");
  backend_.tabs["T4"] = TabFacts{"", GURL("about:blank")};
  facade_.OnTargetAdded("T4");
  EXPECT_EQ(Events("Target.attachedToTarget").size(), 2u);
}

TEST_F(KoanAgentFacadeTest, TakeOverNeedsDriveAndOnlyTheUserResumes) {
  Send(1, "Koan.takeOver");
  EXPECT_EQ(ErrorCode(1), "Koan.notPermitted");
  EXPECT_FALSE(backend_.take_over_asked);
  Send(2, "Koan.takeOver", base::DictValue(), std::nullopt, /*drive=*/true);
  EXPECT_TRUE(backend_.take_over_asked);
  EXPECT_EQ(ErrorCode(2), "Koan.denied");

  registry_.GrantUserTab("U1", "alice", TabState::kDriven,
                         "https://mail.example");
  registry_.Pause("U1");
  Send(3, "Koan.resume", base::DictValue().Set("targetId", "U1"));
  EXPECT_EQ(registry_.Find("U1")->state, TabState::kPaused);
  EXPECT_EQ(backend_.resume_asked, std::vector<std::string>{"U1"});
  EXPECT_EQ(Answer(3)->FindBoolByDottedPath("result.asked"), true);

  // Releasing is for user tabs; the agent's own are closed instead.
  Send(4, "Koan.release", base::DictValue().Set("targetId", "T1"));
  EXPECT_EQ(ErrorCode(4), "Koan.unknownTarget");
  Send(5, "Koan.release", base::DictValue().Set("targetId", "U1"));
  EXPECT_EQ(backend_.released, std::vector<std::string>{"U1"});
}

TEST_F(KoanAgentFacadeTest, AUserTabIsDrivenOnItsSiteOnly) {
  registry_.GrantUserTab("U1", "alice", TabState::kDriven,
                         "https://mail.example");
  const std::string session = AttachTo("U1");
  Send(1, "Page.navigate",
       base::DictValue().Set("url", "https://bank.example/transfer"), session);
  EXPECT_EQ(ErrorCode(1), "Koan.offSite");
  EXPECT_TRUE(backend_.sent.empty());
  Send(2, "Page.navigate",
       base::DictValue().Set("url", "https://mail.example/inbox"), session);
  EXPECT_EQ(backend_.sent.size(), 1u);
  // The agent's own tabs go anywhere the URL filter allows.
  const std::string own = AttachTo("T1");
  Send(3, "Page.navigate",
       base::DictValue().Set("url", "https://bank.example/"), own);
  EXPECT_EQ(backend_.sent.size(), 2u);
}

// The default: the user's yes covers wherever the tab goes.
TEST_F(KoanAgentFacadeTest, AUserTabIsDrivenAnywhereWithoutTheLimit) {
  registry_.GrantUserTab("U1", "alice", TabState::kDriven,
                         "https://mail.example", /*any_site=*/true);
  const std::string session = AttachTo("U1");
  Send(1, "Page.navigate",
       base::DictValue().Set("url", "https://bank.example/transfer"), session);
  EXPECT_EQ(ErrorCode(1), "");
  EXPECT_EQ(backend_.sent.size(), 1u);
  // The URL filter still applies.
  Send(2, "Page.navigate", base::DictValue().Set("url", "chrome://settings"),
       session);
  EXPECT_FALSE(ErrorCode(2).empty());
  EXPECT_EQ(backend_.sent.size(), 1u);
}

TEST_F(KoanAgentFacadeTest, AReadableUserTabHidesSecretInputValues) {
  registry_.GrantUserTab("U1", "alice", TabState::kReadable);
  const std::string session = AttachTo("U1");
  Send(1, "DOM.getOuterHTML", base::DictValue().Set("nodeId", 1), session);
  AnswerLast(base::DictValue().Set(
      "outerHTML", R"(<input type="hidden" name="csrf" value="tok3n">)"));
  EXPECT_EQ(*Answer(1)->FindStringByDottedPath("result.outerHTML"),
            R"(<input type="hidden" name="csrf" value="">)");
  // The agent's own tab is not redacted.
  const std::string own = AttachTo("T1");
  Send(2, "DOM.getOuterHTML", base::DictValue().Set("nodeId", 1), own);
  AnswerLast(base::DictValue().Set("outerHTML",
                                   R"(<input type="hidden" value="mine">)"));
  EXPECT_EQ(*Answer(2)->FindStringByDottedPath("result.outerHTML"),
            R"(<input type="hidden" value="mine">)");
}

TEST_F(KoanAgentFacadeTest, ACommandThePauseOvertookIsNotSent) {
  const std::string session = AttachTo("T1");
  Send(1, "Input.insertText", base::DictValue().Set("text", "hi"), session);
  const int id = *backend_.sent.back().command.FindInt("id");
  EXPECT_TRUE(facade_.MayDispatch(id));
  // The user typed while the command waited for the lease.
  registry_.Pause("T1");
  facade_.OnTargetStateChanged("T1", "Koan.paused", "user-input");
  ASSERT_EQ(Events("Koan.paused").size(), 1u);
  EXPECT_EQ(*Events("Koan.paused")[0]->FindStringByDottedPath("params.state"),
            "paused");
  EXPECT_FALSE(facade_.MayDispatch(id));
  facade_.OnCommandDropped(id);
  EXPECT_EQ(ErrorCode(1), "Koan.paused");
  // An interstitial that came up while a command waited stops it too.
  registry_.Resume("T1");
  Send(3, "Input.insertText", base::DictValue().Set("text", "x"), session);
  const int waiting = *backend_.sent.back().command.FindInt("id");
  backend_.tabs["T1"].interstitial = true;
  EXPECT_FALSE(facade_.MayDispatch(waiting));
  facade_.OnCommandDropped(waiting);
  EXPECT_EQ(ErrorCode(3), "Koan.interstitial");
  backend_.tabs["T1"].interstitial = false;
  // Reads still work on the paused tab.
  registry_.Pause("T1");
  Send(2, "Page.captureScreenshot", base::DictValue(), session);
  EXPECT_TRUE(facade_.MayDispatch(*backend_.sent.back().command.FindInt("id")));
}

// --- Keeping a tab rendering (design section 8) ------------------------------

// A screencast holds the tab's lease from Page.startScreencast until
// Page.stopScreencast; a second start is not a second hold, and a stop with
// none open releases nothing.
TEST_F(KoanAgentFacadeTest, AScreencastHoldsTheTabUntilItStops) {
  const std::string session = AttachTo("T1");
  Send(1, "Page.startScreencast", base::DictValue().Set("format", "jpeg"),
       session);
  EXPECT_EQ(backend_.holds["T1"], 1);
  AnswerLast(base::DictValue());
  Send(2, "Page.startScreencast", base::DictValue(), session);
  EXPECT_EQ(backend_.holds["T1"], 1);
  Send(3, "Page.stopScreencast", base::DictValue(), session);
  EXPECT_EQ(backend_.holds["T1"], 0);
  Send(4, "Page.stopScreencast", base::DictValue(), session);
  EXPECT_EQ(backend_.holds["T1"], 0);
}

TEST_F(KoanAgentFacadeTest, AFailedScreencastReleasesItsHold) {
  const std::string session = AttachTo("T1");
  Send(1, "Page.startScreencast", base::DictValue(), session);
  ASSERT_EQ(backend_.holds["T1"], 1);
  base::DictValue answer;
  answer.Set("id", *backend_.sent.back().command.FindInt("id"));
  answer.Set("error",
             base::DictValue().Set("code", -32000).Set("message", "no"));
  facade_.OnTabMessage("T1", *base::WriteJson(answer));
  EXPECT_EQ(backend_.holds["T1"], 0);
  EXPECT_TRUE(Answer(1)->FindDict("error"));
}

// The release paths of a screencast's hold: the agent detaches its session,
// content detaches the client (the tab navigated where the agent may not
// follow), the tab leaves the agent (closed, Stop, the task ended).
TEST_F(KoanAgentFacadeTest, AScreencastEndsWithItsSession) {
  std::string session = AttachTo("T1");
  Send(1, "Page.startScreencast", base::DictValue(), session);
  Send(2, "Target.detachFromTarget",
       base::DictValue().Set("sessionId", session));
  EXPECT_EQ(backend_.holds["T1"], 0);

  session = AttachTo("T1");
  Send(3, "Page.startScreencast", base::DictValue(), session);
  ASSERT_EQ(backend_.holds["T1"], 1);
  facade_.OnTabDetached("T1");
  EXPECT_EQ(backend_.holds["T1"], 0);

  session = AttachTo("T1");
  Send(4, "Page.startScreencast", base::DictValue(), session);
  ASSERT_EQ(backend_.holds["T1"], 1);
  registry_.Remove("T1");
  facade_.OnTargetRemoved("T1");
  EXPECT_EQ(backend_.holds["T1"], 0);
}

// The connection goes (the agent disconnected or was revoked, Agent ready
// off): every screencast hold and keep-alive of it ends before its clients
// are detached.
TEST_F(KoanAgentFacadeTest, EverythingHeldEndsWithTheConnection) {
  auto facade = std::make_unique<CdpFacade>(
      8, "alice", &registry_, &backend_, &clock_,
      base::BindLambdaForTesting(
          [this](std::string json) { OnSend(std::move(json)); }));
  AgentMessage attach;
  attach.id = 1;
  attach.method = "Target.attachToTarget";
  attach.params = base::DictValue().Set("targetId", "T1").Set("flatten", true);
  attach.client_id = "alice";
  facade->OnClientMessage(std::move(attach));
  const std::string* session =
      Answer(1)->FindStringByDottedPath("result.sessionId");
  ASSERT_TRUE(session);
  AgentMessage start;
  start.id = 2;
  start.method = "Page.startScreencast";
  start.session_id = *session;
  start.client_id = "alice";
  facade->OnClientMessage(std::move(start));
  AgentMessage keep;
  keep.id = 3;
  keep.method = "Koan.keepAlive";
  keep.params = base::DictValue().Set("targetId", "T1").Set("seconds", 30);
  keep.client_id = "alice";
  facade->OnClientMessage(std::move(keep));
  ASSERT_EQ(backend_.holds["T1"], 1);
  ASSERT_EQ(backend_.keep_alives.size(), 1u);

  facade.reset();
  EXPECT_EQ(backend_.holds["T1"], 0);
  EXPECT_TRUE(backend_.keep_alives.empty());
  EXPECT_FALSE(backend_.attached.contains("T1"));
}

TEST_F(KoanAgentFacadeTest, KeepAliveIsGrantedRenewedAndEnded) {
  Send(1, "Koan.keepAlive",
       base::DictValue().Set("targetId", "T1").Set("seconds", 30));
  const std::string* hold = Answer(1)->FindStringByDottedPath("result.holdId");
  ASSERT_TRUE(hold);
  const std::string hold_id = *hold;
  EXPECT_EQ(backend_.keep_alives[hold_id].target_id, "T1");
  EXPECT_EQ(backend_.keep_alives[hold_id].duration, base::Seconds(30));
  // No session needed: a CLI waiting on the tab may hold it on its own.
  EXPECT_FALSE(backend_.attached.contains("T1"));

  // A second one is a hold of its own, so ending one does not end the other.
  Send(2, "Koan.keepAlive",
       base::DictValue().Set("targetId", "T1").Set("seconds", 30));
  const std::string other = *Answer(2)->FindStringByDottedPath("result.holdId");
  EXPECT_NE(other, hold_id);

  clock_.Advance(base::Seconds(20));
  Send(3, "Koan.keepAlive",
       base::DictValue().Set("holdId", hold_id).Set("seconds", 45.5));
  EXPECT_EQ(*Answer(3)->FindStringByDottedPath("result.holdId"), hold_id);
  EXPECT_EQ(backend_.keep_alives[hold_id].duration, base::Seconds(45.5));

  Send(4, "Koan.keepAlive",
       base::DictValue().Set("holdId", hold_id).Set("seconds", 0));
  EXPECT_TRUE(Answer(4)->FindDict("result"));
  EXPECT_FALSE(backend_.keep_alives.contains(hold_id));
  EXPECT_TRUE(backend_.keep_alives.contains(other));
  // Ended: it cannot be renewed.
  Send(5, "Koan.keepAlive",
       base::DictValue().Set("holdId", hold_id).Set("seconds", 10));
  EXPECT_EQ(ErrorCode(5), "Koan.invalidParams");
  EXPECT_FALSE(backend_.keep_alives.contains(hold_id));
}

TEST_F(KoanAgentFacadeTest, KeepAliveIsRefusedWhereItMustBe) {
  // Not the client's tab, or none.
  Send(1, "Koan.keepAlive",
       base::DictValue().Set("targetId", "T2").Set("seconds", 30));
  EXPECT_EQ(ErrorCode(1), "Koan.unknownTarget");
  Send(2, "Koan.keepAlive", base::DictValue().Set("seconds", 30));
  EXPECT_EQ(ErrorCode(2), "Koan.unknownTarget");
  // Seconds out of range, or missing.
  for (int id : {3, 4, 5}) {
    base::DictValue params;
    params.Set("targetId", "T1");
    if (id == 3) {
      params.Set("seconds", -1);
    } else if (id == 4) {
      params.Set("seconds", 601);
    }
    Send(id, "Koan.keepAlive", std::move(params));
    EXPECT_EQ(ErrorCode(id), "Koan.invalidParams") << id;
  }
  // Someone else's hold id is not found by guessing.
  Send(6, "Koan.keepAlive",
       base::DictValue().Set("holdId", "1.1").Set("seconds", 30));
  EXPECT_EQ(ErrorCode(6), "Koan.invalidParams");
  EXPECT_TRUE(backend_.keep_alives.empty());

  // On battery in Low Power Mode: refused above 60 s, with the event.
  backend_.low_power = true;
  Send(7, "Koan.keepAlive",
       base::DictValue().Set("targetId", "T1").Set("seconds", 120));
  EXPECT_EQ(ErrorCode(7), "Koan.lowPower");
  ASSERT_EQ(Events("Koan.lowPower").size(), 1u);
  const base::DictValue& event = *Events("Koan.lowPower")[0];
  EXPECT_EQ(*event.FindStringByDottedPath("params.reason"),
            "battery-low-power-mode");
  EXPECT_EQ(event.FindIntByDottedPath("params.maxLeases"), 2);
  EXPECT_EQ(event.FindIntByDottedPath("params.maxKeepAliveSeconds"), 60);
  EXPECT_TRUE(backend_.keep_alives.empty());
  Send(8, "Koan.keepAlive",
       base::DictValue().Set("targetId", "T1").Set("seconds", 60));
  EXPECT_TRUE(Answer(8)->FindDict("result"));
}

// At most 16 open per connection; one that lapsed, or whose tab went, no
// longer counts.
TEST_F(KoanAgentFacadeTest, KeepAlivesAreLimited) {
  for (int id = 1; id <= 16; ++id) {
    Send(id, "Koan.keepAlive",
         base::DictValue().Set("targetId", "T1").Set("seconds", 10));
    ASSERT_TRUE(Answer(id)->FindDict("result")) << id;
  }
  Send(17, "Koan.keepAlive",
       base::DictValue().Set("targetId", "T1").Set("seconds", 10));
  EXPECT_EQ(ErrorCode(17), "Koan.tooManyHolds");
  clock_.Advance(base::Seconds(10));
  Send(18, "Koan.keepAlive",
       base::DictValue().Set("targetId", "T1").Set("seconds", 600));
  EXPECT_TRUE(Answer(18)->FindDict("result"));

  registry_.AddAgentTab("T3", "alice", "task");
  backend_.tabs["T3"] = TabFacts{"Third", GURL("https://example.com/c")};
  for (int id = 19; id <= 33; ++id) {
    Send(id, "Koan.keepAlive",
         base::DictValue().Set("targetId", "T3").Set("seconds", 600));
  }
  Send(34, "Koan.keepAlive",
       base::DictValue().Set("targetId", "T1").Set("seconds", 10));
  EXPECT_EQ(ErrorCode(34), "Koan.tooManyHolds");
  registry_.Remove("T3");
  facade_.OnTargetRemoved("T3");
  Send(35, "Koan.keepAlive",
       base::DictValue().Set("targetId", "T1").Set("seconds", 10));
  EXPECT_TRUE(Answer(35)->FindDict("result"));
}

// Auto-resume (design 9.2): Koan.paused says whether the pause waits for
// Hand back or when it resumes by itself, Koan.listTabs too, and
// Koan.resumed says how long it lasted and what the user left.
TEST_F(KoanAgentFacadeTest, PauseAndResumeEventsDescribeThePause) {
  registry_.Pause("T1");
  const base::Time at = base::Time::UnixEpoch() + base::Seconds(1000);
  registry_.SetPause("T1", {.reason = "user-input", .resumes_at = at});
  facade_.OnTargetStateChanged("T1", "Koan.paused", "user-input");
  ASSERT_EQ(Events("Koan.paused").size(), 1u);
  const base::DictValue* paused = Events("Koan.paused")[0]->FindDict("params");
  EXPECT_EQ(*paused->FindString("reason"), "user-input");
  EXPECT_EQ(*paused->FindString("pauseReason"), "user-input");
  EXPECT_FALSE(paused->FindString("sticky"));
  EXPECT_EQ(paused->FindDouble("resumesAt"), 1000000.0);

  Send(1, "Koan.listTabs");
  const base::ListValue* tabs = Answer(1)->FindListByDottedPath("result.tabs");
  ASSERT_TRUE(tabs);
  ASSERT_EQ(tabs->size(), 1u);
  const base::DictValue& tab = (*tabs)[0].GetDict();
  EXPECT_EQ(tab.FindBool("paused"), true);
  EXPECT_EQ(tab.FindDoubleByDottedPath("pause.resumesAt"), 1000000.0);

  // Held: sticky with why, and no resume time.
  registry_.SetPause("T1", {.reason = "user-input", .sticky = "secret-field"});
  facade_.OnTargetStateChanged("T1", "Koan.paused", "user-input");
  paused = Events("Koan.paused")[1]->FindDict("params");
  EXPECT_EQ(*paused->FindString("sticky"), "secret-field");
  EXPECT_FALSE(paused->FindDouble("resumesAt"));

  registry_.Resume("T1");
  backend_.tabs["T1"] = TabFacts{"Left", GURL("https://example.com/b")};
  facade_.OnTargetResumed("T1", {.reason = "user-idle",
                                 .paused_for = base::Milliseconds(6500),
                                 .navigated = true});
  ASSERT_EQ(Events("Koan.resumed").size(), 1u);
  const base::DictValue* resumed =
      Events("Koan.resumed")[0]->FindDict("params");
  EXPECT_EQ(*resumed->FindString("targetId"), "T1");
  EXPECT_EQ(*resumed->FindString("state"), "driven");
  EXPECT_EQ(*resumed->FindString("reason"), "user-idle");
  EXPECT_EQ(resumed->FindDouble("pausedMs"), 6500.0);
  EXPECT_EQ(*resumed->FindString("url"), "https://example.com/b");
  EXPECT_EQ(*resumed->FindString("title"), "Left");
  EXPECT_EQ(resumed->FindBool("navigated"), true);
  // A navigation is a change.
  EXPECT_EQ(resumed->FindBool("changed"), true);

  Send(2, "Koan.listTabs");
  tabs = Answer(2)->FindListByDottedPath("result.tabs");
  EXPECT_EQ((*tabs)[0].GetDict().FindBool("paused"), false);
  EXPECT_FALSE((*tabs)[0].GetDict().FindDict("pause"));

  // Another client's tab is never told about.
  facade_.OnTargetResumed("T2", {.reason = "handed-back"});
  EXPECT_EQ(Events("Koan.resumed").size(), 1u);
}

// --- Secrets Koan filled (design section 4.5) ------------------------------

TEST_F(KoanAgentFacadeTest, ScriptWaitsWhileASecretIsFilledInThePage) {
  const std::string session = AttachTo("T1");
  backend_.facts.locked = true;
  for (const char* method : {"Runtime.evaluate", "Runtime.callFunctionOn",
                             "Runtime.getProperties", "Runtime.awaitPromise"}) {
    Send(1, method,
         base::DictValue()
             .Set("expression", "document.querySelector('input').value")
             .Set("objectId", "o")
             .Set("functionDeclaration", "function(){return this.value}")
             .Set("promiseObjectId", "p"),
         session);
    EXPECT_EQ(ErrorCode(1), "Koan.secretField") << method;
  }
  EXPECT_TRUE(backend_.sent.empty());
  // Clicks, keys elsewhere, snapshots and navigation still go.
  Send(2, "Input.dispatchMouseEvent",
       base::DictValue()
           .Set("type", "mousePressed")
           .Set("x", 1)
           .Set("y", 1)
           .Set("button", "left"),
       session);
  EXPECT_EQ(ErrorCode(2), "");
  Send(3, "Input.insertText", base::DictValue().Set("text", "Ada"), session);
  EXPECT_EQ(ErrorCode(3), "");
  Send(4, "Accessibility.getFullAXTree", base::DictValue(), session);
  EXPECT_EQ(ErrorCode(4), "");
  // Typing into the filled field itself does not; Enter, Tab and keyUp do.
  backend_.facts.focused_locked = true;
  Send(5, "Input.insertText", base::DictValue().Set("text", "x"), session);
  EXPECT_EQ(ErrorCode(5), "Koan.secretField");
  Send(6, "Input.dispatchKeyEvent",
       base::DictValue().Set("type", "keyDown").Set("key", "Backspace"),
       session);
  EXPECT_EQ(ErrorCode(6), "Koan.secretField");
  Send(7, "Input.dispatchKeyEvent",
       base::DictValue().Set("type", "char").Set("text", "a"), session);
  EXPECT_EQ(ErrorCode(7), "Koan.secretField");
  Send(8, "Input.dispatchKeyEvent",
       base::DictValue()
           .Set("type", "keyDown")
           .Set("key", "Enter")
           .Set("text", "\r"),
       session);
  EXPECT_EQ(ErrorCode(8), "");
  Send(9, "Input.dispatchKeyEvent",
       base::DictValue().Set("type", "keyUp").Set("key", "a"), session);
  EXPECT_EQ(ErrorCode(9), "");

  // The page moved on (or the field was cleared): script runs again, once
  // the tab has handled the input before it.
  std::vector<std::pair<std::string, int>> unanswered;
  for (const FakeBackend::Sent& sent : backend_.sent) {
    unanswered.emplace_back(sent.target_id, *sent.command.FindInt("id"));
  }
  for (const auto& [target, id] : unanswered) {
    facade_.OnTabMessage(target,
                         *base::WriteJson(base::DictValue().Set("id", id).Set(
                             "result", base::DictValue())));
  }
  backend_.facts = FacadeBackend::SecretFacts();
  Send(10, "Runtime.evaluate", base::DictValue().Set("expression", "1"),
       session);
  EXPECT_EQ(*backend_.sent.back().command.FindString("method"),
            "Runtime.evaluate");
  EXPECT_EQ(ErrorCode(10), "");
  // Each refusal is audited, without the script.
  bool audited = false;
  for (const AuditRecord& record : backend_.audits) {
    audited = audited || (record.event == "Runtime.evaluate" &&
                          record.outcome == AuditOutcome::kDenied);
    EXPECT_EQ(record.summary.find("querySelector"), std::string::npos);
  }
  EXPECT_TRUE(audited);
}

// S1 (#412 review): a password Koan did not fill (the password manager's on
// load, or one the user typed) locks script too. Before a script read the
// backend reads the fields if anything changed them; the read's commands,
// and those after them, wait in order.
TEST_F(KoanAgentFacadeTest, ScriptWaitsForTheSecretFieldsToBeRead) {
  const std::string session = AttachTo("T1");
  backend_.secrets_current = false;
  // Not script: no read.
  Send(1, "DOM.getDocument", base::DictValue(), session);
  ASSERT_EQ(backend_.sent.size(), 1u);
  EXPECT_TRUE(backend_.refreshes.empty());
  // Script waits, and so does what comes after it.
  Send(2, "Runtime.evaluate", base::DictValue().Set("expression", "1"),
       session);
  Send(3, "DOM.getDocument", base::DictValue(), session);
  EXPECT_EQ(backend_.sent.size(), 1u);
  ASSERT_EQ(backend_.refreshes.size(), 1u);
  EXPECT_EQ(Answer(2), nullptr);
  // The read found a password the user typed: refused, then the next goes.
  backend_.facts.locked = true;
  backend_.secrets_current = true;
  std::move(backend_.refreshes[0]).Run();
  EXPECT_EQ(ErrorCode(2), "Koan.secretField");
  ASSERT_EQ(backend_.sent.size(), 2u);
  EXPECT_EQ(*backend_.sent[1].command.FindString("method"), "DOM.getDocument");
  // A read that is current is not made again.
  backend_.facts = FacadeBackend::SecretFacts();
  Send(4, "Runtime.evaluate", base::DictValue().Set("expression", "1"),
       session);
  EXPECT_EQ(backend_.refreshes.size(), 1u);
  EXPECT_EQ(backend_.sent.size(), 3u);
  // A frame that did not answer counts as locked.
  backend_.facts.unknown = true;
  Send(5, "Runtime.callFunctionOn",
       base::DictValue()
           .Set("objectId", "o")
           .Set("functionDeclaration", "function(){return this.value}"),
       session);
  EXPECT_EQ(ErrorCode(5), "Koan.secretField");
}

TEST_F(KoanAgentFacadeTest, ScriptWaitsForTheInputBeforeIt) {
  const std::string session = AttachTo("T1");
  // A click (which may show a password filled on load) is in flight.
  Send(1, "Input.dispatchMouseEvent",
       base::DictValue()
           .Set("type", "mousePressed")
           .Set("x", 1)
           .Set("y", 1)
           .Set("button", "left"),
       session);
  ASSERT_EQ(backend_.sent.size(), 1u);
  Send(2, "Runtime.evaluate", base::DictValue().Set("expression", "1"),
       session);
  EXPECT_EQ(backend_.sent.size(), 1u);
  // Its answer: the fields may have changed, and they are read first.
  backend_.secrets_current = false;
  AnswerLast(base::DictValue());
  EXPECT_EQ(backend_.secrets_changed, 1);
  EXPECT_EQ(ErrorCode(1), "");
  ASSERT_EQ(backend_.refreshes.size(), 1u);
  EXPECT_EQ(backend_.sent.size(), 1u);
  backend_.secrets_current = true;
  std::move(backend_.refreshes[0]).Run();
  ASSERT_EQ(backend_.sent.size(), 2u);
  EXPECT_EQ(*backend_.sent[1].command.FindString("method"), "Runtime.evaluate");
}

TEST_F(KoanAgentFacadeTest, CommandsThatWaitEndWithTheSession) {
  const std::string session = AttachTo("T1");
  backend_.secrets_current = false;
  Send(1, "Runtime.evaluate", base::DictValue().Set("expression", "1"),
       session);
  Send(2, "Page.reload", base::DictValue(), session);
  facade_.OnTabDetached("T1");
  EXPECT_EQ(ErrorCode(1), "Koan.targetClosed");
  EXPECT_EQ(ErrorCode(2), "Koan.targetClosed");
  // The read's answer after that is nothing.
  ASSERT_EQ(backend_.refreshes.size(), 1u);
  std::move(backend_.refreshes[0]).Run();
  EXPECT_TRUE(backend_.sent.empty());
}

// DOMSnapshot carries the live value of every input: its domain stays denied.
TEST_F(KoanAgentFacadeTest, DomSnapshotIsDenied) {
  const std::string session = AttachTo("T1");
  for (const char* method :
       {"DOMSnapshot.captureSnapshot", "DOMSnapshot.getSnapshot"}) {
    Send(1, method, base::DictValue(), session);
    EXPECT_EQ(ErrorCode(1), "Koan.notPermitted") << method;
  }
  EXPECT_TRUE(backend_.sent.empty());
}

TEST_F(KoanAgentFacadeTest, ACardFilledStopsPrintAndScreencast) {
  const std::string session = AttachTo("T1");
  backend_.facts.card_filled = true;
  Send(1, "Page.printToPDF", base::DictValue(), session);
  EXPECT_EQ(ErrorCode(1), "Koan.secretField");
  Send(2, "Page.startScreencast", base::DictValue(), session);
  EXPECT_EQ(ErrorCode(2), "Koan.secretField");
  EXPECT_TRUE(backend_.sent.empty());
  // A screencast that was running drops its frames (acknowledged, so it
  // does not stall) while the card is filled.
  TabEvent("T1", "Page.screencastFrame",
           base::DictValue().Set("data", "AAAA").Set("sessionId", 3));
  EXPECT_TRUE(Events("Page.screencastFrame").empty());
  ASSERT_EQ(backend_.sent.size(), 1u);
  EXPECT_EQ(*backend_.sent[0].command.FindString("method"),
            "Page.screencastFrameAck");
}

TEST_F(KoanAgentFacadeTest, SecretFieldsAreHiddenOnTheAgentsOwnTab) {
  const std::string session = AttachTo("T1");
  backend_.facts.fields = {41};
  Send(1, "Accessibility.getFullAXTree", base::DictValue(), session);
  AnswerLast(Parse(R"({"nodes": [
      {"nodeId": "2", "role": {"value": "textbox"}, "backendDOMNodeId": 41,
       "value": {"type": "string", "value": "4111111111111111"}}]})"));
  const base::ListValue* nodes =
      Answer(1)->FindListByDottedPath("result.nodes");
  ASSERT_TRUE(nodes);
  EXPECT_EQ(*(*nodes)[0].GetDict().FindStringByDottedPath("value.value"),
            kSecretFilled);
  // A password field's value attribute, whatever the agent typed.
  Send(2, "DOM.getAttributes", base::DictValue().Set("nodeId", 9), session);
  AnswerLast(Parse(R"({"attributes": ["type", "password", "value", "pw"]})"));
  EXPECT_EQ(base::WriteJson(*Answer(2))->find("\"pw\""), std::string::npos);
  // A card field's, by its autocomplete, in every DOM read.
  Send(3, "DOM.getAttributes", base::DictValue().Set("nodeId", 10), session);
  AnswerLast(Parse(R"({"attributes": ["autocomplete", "cc-number",
      "value", "4111111111111111"]})"));
  Send(4, "DOM.describeNode", base::DictValue().Set("nodeId", 10), session);
  AnswerLast(Parse(R"({"node": {"nodeId": 10, "backendNodeId": 50,
      "nodeName": "INPUT", "attributes": ["autocomplete", "cc-number",
      "value", "4111111111111111"]}})"));
  Send(5, "DOM.getOuterHTML", base::DictValue().Set("nodeId", 1), session);
  AnswerLast(Parse(
      R"({"outerHTML": "<form><input autocomplete='cc-number' )"
      R"(value='4111111111111111'><input type=password value=pw2></form>"})"));
  // A field autofill classifies as a card, whatever its markup says.
  Send(6, "DOM.describeNode", base::DictValue().Set("nodeId", 11), session);
  AnswerLast(Parse(R"({"node": {"nodeId": 11, "backendNodeId": 41,
      "nodeName": "INPUT", "attributes": ["name", "n", "value",
      "4111111111111111"]}})"));
  for (int id : {3, 4, 5, 6}) {
    const std::string json = *base::WriteJson(*Answer(id));
    EXPECT_EQ(json.find("4111"), std::string::npos) << json;
    EXPECT_EQ(json.find("pw2"), std::string::npos) << json;
  }
}

TEST_F(KoanAgentFacadeTest, NothingSentCarriesASecretKoanFilled) {
  const std::string session = AttachTo("T1");
  ASSERT_TRUE(backend_.scrubber.Add("hunter2-secret"));
  // A console message, an answer, a request body.
  TabEvent("T1", "Runtime.consoleAPICalled", Parse(R"({"type": "log", "args": [
               {"type": "string", "value": "pw=hunter2-secret"}]})"));
  Send(1, "Accessibility.getFullAXTree", base::DictValue(), session);
  AnswerLast(Parse(R"({"nodes": [{"nodeId": "1",
      "name": {"value": "hunter2-secret"}}]})"));
  TabEvent("T1", "Network.requestWillBeSent",
           Parse(R"({"requestId": "1", "request": {
               "url": "https://example.com/login?p=hunter2-secret",
               "headers": {}}})"));
  for (const base::DictValue& message : received_) {
    EXPECT_EQ(base::WriteJson(message)->find("hunter2"), std::string::npos)
        << *base::WriteJson(message);
  }
  EXPECT_NE(base::WriteJson(*Answer(1))->find("[secret]"), std::string::npos);
}

TEST_F(KoanAgentFacadeTest, AutofillOnlyOnADrivenTab) {
  const std::string session = AttachTo("T1");
  // No session, or a bad request.
  Send(1, "Koan.autofill",
       base::DictValue().Set("backendNodeId", 4).Set("list", true));
  EXPECT_EQ(ErrorCode(1), "Koan.invalidParams");
  Send(2, "Koan.autofill", base::DictValue().Set("list", true), session);
  EXPECT_EQ(ErrorCode(2), "Koan.invalidParams");
  Send(3, "Koan.autofill",
       base::DictValue()
           .Set("backendNodeId", 4)
           .Set("list", true)
           .Set("pick", 1),
       session);
  EXPECT_EQ(ErrorCode(3), "Koan.invalidParams");
  Send(4, "Koan.autofill",
       base::DictValue().Set("backendNodeId", 4).Set("pick", 0), session);
  EXPECT_EQ(ErrorCode(4), "Koan.invalidParams");
  EXPECT_TRUE(backend_.autofills.empty());

  // Paused, an interstitial.
  registry_.Pause("T1");
  Send(5, "Koan.autofill",
       base::DictValue().Set("backendNodeId", 4).Set("list", true), session);
  EXPECT_EQ(ErrorCode(5), "Koan.paused");
  registry_.Resume("T1");
  backend_.tabs["T1"].interstitial = true;
  Send(6, "Koan.autofill",
       base::DictValue().Set("backendNodeId", 4).Set("list", true), session);
  EXPECT_EQ(ErrorCode(6), "Koan.interstitial");
  backend_.tabs["T1"].interstitial = false;
  EXPECT_TRUE(backend_.autofills.empty());

  // A pick goes to the backend and is answered when it is done; its audit
  // says the field's kind and the row's label, never a value.
  Send(7, "Koan.autofill",
       base::DictValue().Set("backendNodeId", 4).Set("pickLabel", "Visa"),
       session);
  ASSERT_EQ(backend_.autofills.size(), 1u);
  EXPECT_EQ(backend_.autofills[0].target_id, "T1");
  EXPECT_EQ(backend_.autofills[0].frame_id, "");
  EXPECT_EQ(backend_.autofills[0].request.node, 4);
  EXPECT_EQ(backend_.autofills[0].request.pick.label, "Visa");
  EXPECT_FALSE(backend_.autofills[0].request.list);
  FacadeBackend::AutofillResult result;
  result.result.Set("filled", true);
  result.result.Set("kind", "card");
  result.summary = "card Visa ••••4242";
  std::move(backend_.autofill_done).Run(std::move(result));
  EXPECT_EQ(Answer(7)->FindBoolByDottedPath("result.filled"), true);
  EXPECT_EQ(backend_.audits.back().event, "Koan.autofill");
  EXPECT_EQ(backend_.audits.back().summary, "card Visa ••••4242");
  EXPECT_EQ(backend_.audits.back().outcome, AuditOutcome::kOk);

  // A refusal comes back as the backend's error.
  Send(8, "Koan.autofill",
       base::DictValue().Set("backendNodeId", 4).Set("pick", 2), session);
  FacadeBackend::AutofillResult refused;
  refused.error_code = "Koan.offSite";
  refused.error_message = "no";
  std::move(backend_.autofill_done).Run(std::move(refused));
  EXPECT_EQ(ErrorCode(8), "Koan.offSite");
  EXPECT_EQ(backend_.audits.back().outcome, AuditOutcome::kDenied);
}

TEST_F(KoanAgentFacadeTest, AutofillOnAReadableTabIsRefused) {
  registry_.GrantUserTab("U1", "alice", TabState::kReadable);
  const std::string session = AttachTo("U1");
  Send(1, "Koan.autofill",
       base::DictValue().Set("backendNodeId", 4).Set("list", true), session);
  EXPECT_EQ(ErrorCode(1), "Koan.notPermitted");
  EXPECT_TRUE(backend_.autofills.empty());
}

// `koan upload`: only on a tab the agent drives, with absolute paths, and
// audited with the files' names and sizes only.
TEST_F(KoanAgentFacadeTest, UploadOnlyOnADrivenTab) {
  const std::string session = AttachTo("T1");
  auto files = [](std::initializer_list<std::string_view> paths) {
    base::ListValue list;
    for (std::string_view path : paths) {
      list.Append(base::DictValue().Set("path", path));
    }
    return list;
  };
  // No session, no files, a relative path, a name with a separator, too
  // long a wait, too many files.
  Send(1, "Koan.upload", base::DictValue().Set("files", files({"/a.pdf"})));
  EXPECT_EQ(ErrorCode(1), "Koan.invalidParams");
  Send(2, "Koan.upload", base::DictValue(), session);
  EXPECT_EQ(ErrorCode(2), "Koan.invalidParams");
  Send(3, "Koan.upload", base::DictValue().Set("files", files({"cv.pdf"})),
       session);
  EXPECT_EQ(ErrorCode(3), "Koan.invalidParams");
  base::ListValue renamed;
  renamed.Append(
      base::DictValue().Set("path", "/tmp/cv.pdf").Set("name", "../cv.pdf"));
  Send(4, "Koan.upload", base::DictValue().Set("files", std::move(renamed)),
       session);
  EXPECT_EQ(ErrorCode(4), "Koan.invalidParams");
  Send(5, "Koan.upload",
       base::DictValue()
           .Set("files", files({"/tmp/cv.pdf"}))
           .Set("waitMs", 60000),
       session);
  EXPECT_EQ(ErrorCode(5), "Koan.invalidParams");
  base::ListValue many;
  for (int i = 0; i < 33; ++i) {
    many.Append(base::DictValue().Set("path", "/tmp/x.png"));
  }
  Send(6, "Koan.upload", base::DictValue().Set("files", std::move(many)),
       session);
  EXPECT_EQ(ErrorCode(6), "Koan.invalidParams");
  EXPECT_TRUE(backend_.uploads.empty());

  // Paused (the user took the tab), an interstitial.
  registry_.Pause("T1");
  Send(7, "Koan.upload", base::DictValue().Set("files", files({"/tmp/a.pdf"})),
       session);
  EXPECT_EQ(ErrorCode(7), "Koan.paused");
  EXPECT_EQ(backend_.audits.back().outcome, AuditOutcome::kPaused);
  registry_.Resume("T1");
  backend_.tabs["T1"].interstitial = true;
  Send(8, "Koan.upload", base::DictValue().Set("files", files({"/tmp/a.pdf"})),
       session);
  EXPECT_EQ(ErrorCode(8), "Koan.interstitial");
  backend_.tabs["T1"].interstitial = false;
  EXPECT_TRUE(backend_.uploads.empty());

  // An upload goes to the backend and is answered when it is done; its audit
  // names the files and their sizes.
  base::ListValue two;
  two.Append(base::DictValue().Set("path", "/Users/<user>/cv.pdf"));
  two.Append(base::DictValue().Set("path", "/tmp/x1").Set("name", "cover.pdf"));
  Send(9, "Koan.upload",
       base::DictValue().Set("files", std::move(two)).Set("waitMs", 5000),
       session);
  ASSERT_EQ(backend_.uploads.size(), 1u);
  EXPECT_EQ(backend_.uploads[0].target_id, "T1");
  ASSERT_EQ(backend_.uploads[0].request.files.size(), 2u);
  EXPECT_EQ(backend_.uploads[0].request.files[0].path, "/Users/<user>/cv.pdf");
  EXPECT_EQ(backend_.uploads[0].request.files[0].name, "");
  EXPECT_EQ(backend_.uploads[0].request.files[1].name, "cover.pdf");
  EXPECT_EQ(backend_.uploads[0].request.wait, base::Seconds(5));
  EXPECT_FALSE(Answer(9));
  FacadeBackend::UploadResult result;
  result.result.Set(
      "files", base::ListValue().Append(
                   base::DictValue().Set("name", "cv.pdf").Set("size", 1200)));
  result.summary = "cv.pdf (1.2 KB), cover.pdf (12 B)";
  std::move(backend_.upload_done).Run(std::move(result));
  const base::ListValue* attached =
      Answer(9)->FindListByDottedPath("result.files");
  ASSERT_TRUE(attached);
  EXPECT_EQ(attached->size(), 1u);
  EXPECT_EQ(backend_.audits.back().event, "Koan.upload");
  EXPECT_EQ(backend_.audits.back().summary,
            "cv.pdf (1.2 KB), cover.pdf (12 B)");
  EXPECT_EQ(backend_.audits.back().outcome, AuditOutcome::kOk);

  // A refusal comes back as the backend's error, and is audited as one.
  Send(10, "Koan.upload",
       base::DictValue().Set("files", files({"/tmp/missing.pdf"})), session);
  FacadeBackend::UploadResult refused;
  refused.error_code = "Koan.fileNotFound";
  refused.error_message = "no file at /tmp/missing.pdf";
  std::move(backend_.upload_done).Run(std::move(refused));
  EXPECT_EQ(ErrorCode(10), "Koan.fileNotFound");
  EXPECT_EQ(backend_.audits.back().outcome, AuditOutcome::kDenied);
}

TEST_F(KoanAgentFacadeTest, UploadOnAReadableOrAnotherAgentsTabIsRefused) {
  registry_.GrantUserTab("U1", "alice", TabState::kReadable);
  const std::string session = AttachTo("U1");
  base::ListValue files;
  files.Append(base::DictValue().Set("path", "/tmp/a.pdf"));
  Send(1, "Koan.upload", base::DictValue().Set("files", files.Clone()),
       session);
  EXPECT_EQ(ErrorCode(1), "Koan.notPermitted");
  // Bob's tab is not in alice's scope: she cannot even attach to it.
  EXPECT_EQ(AttachTo("T2"), "");
  EXPECT_TRUE(backend_.uploads.empty());
}

TEST_F(KoanAgentFacadeTest, AScreenshotHidesTheCardFieldsKoanFilled) {
  const std::string session = AttachTo("T1");
  backend_.facts.card_filled = true;
  backend_.card_fields.nodes = {41};
  backend_.card_fields.frames = {"FRAME1"};
  Send(1, "Page.captureScreenshot",
       base::DictValue().Set("format", "jpeg").Set("quality", 70), session);
  // The layout, the payment frame's <iframe>, each box; then the shot.
  ASSERT_EQ(*backend_.sent.back().command.FindString("method"),
            "Page.getLayoutMetrics");
  AnswerLast(Parse(R"({"cssVisualViewport": {"pageX": 0, "pageY": 100,
      "clientWidth": 800, "clientHeight": 600}})"));
  ASSERT_EQ(*backend_.sent.back().command.FindString("method"),
            "DOM.getFrameOwner");
  EXPECT_EQ(
      *backend_.sent.back().command.FindStringByDottedPath("params.frameId"),
      "FRAME1");
  AnswerLast(Parse(R"({"backendNodeId": 50})"));
  ASSERT_EQ(*backend_.sent.back().command.FindString("method"),
            "DOM.getContentQuads");
  // Two quads asked for (41 and the frame's 50); answered in order.
  const size_t sent = backend_.sent.size();
  ASSERT_GE(sent, 2u);
  for (size_t i = sent - 2; i < sent; ++i) {
    base::DictValue answer;
    answer.Set("id", *backend_.sent[i].command.FindInt("id"));
    answer.Set("result",
               Parse(R"({"quads": [[10, 10, 110, 10, 110, 30, 10, 30]]})"));
    facade_.OnTabMessage("T1", *base::WriteJson(answer));
  }
  const base::DictValue& shot = backend_.sent.back().command;
  ASSERT_EQ(*shot.FindString("method"), "Page.captureScreenshot");
  // Taken as PNG, encoded as asked after.
  EXPECT_EQ(*shot.FindStringByDottedPath("params.format"), "png");
  EXPECT_FALSE(shot.FindByDottedPath("params.quality"));
  EXPECT_EQ(ErrorCode(1), "");
  EXPECT_FALSE(Answer(1));
  AnswerLast(base::DictValue().Set("data", "iVBORw0K"));
  ASSERT_EQ(backend_.masks.size(), 1u);
  EXPECT_EQ(backend_.masked_format, "jpeg");
  ASSERT_EQ(backend_.masks[0].boxes.size(), 2u);
  EXPECT_EQ(backend_.masks[0].css_width, 800);
  EXPECT_EQ(*Answer(1)->FindStringByDottedPath("result.data"),
            "masked:iVBORw0K");
  EXPECT_EQ(Answer(1)->FindIntByDottedPath("result.koanMasked"), 2);
}

TEST_F(KoanAgentFacadeTest, AScreenshotThatCannotBeMaskedIsRefused) {
  const std::string session = AttachTo("T1");
  backend_.facts.card_filled = true;
  backend_.card_fields.frames = {"FRAME1"};
  Send(1, "Page.captureScreenshot", base::DictValue(), session);
  AnswerLast(Parse(R"({"cssVisualViewport": {"clientWidth": 800,
      "clientHeight": 600}})"));
  // The frame's owner is not found: no screenshot at all.
  base::DictValue error;
  error.Set("id", *backend_.sent.back().command.FindInt("id"));
  error.Set("error", base::DictValue().Set("code", -32000).Set("message", "x"));
  facade_.OnTabMessage("T1", *base::WriteJson(error));
  EXPECT_EQ(ErrorCode(1), "Koan.secretField");
  for (const FakeBackend::Sent& command : backend_.sent) {
    EXPECT_NE(*command.command.FindString("method"), "Page.captureScreenshot");
  }
}

TEST_F(KoanAgentFacadeTest, AScreenshotDataIsNotScanned) {
  const std::string session = AttachTo("T1");
  // A secret whose base64 run happens to be in the image data: the image is
  // passed over (it is masked instead), the rest of the answer is not.
  ASSERT_TRUE(backend_.scrubber.Add("abcdefgh"));
  Send(1, "Page.captureScreenshot", base::DictValue(), session);
  AnswerLast(base::DictValue().Set("data", "YWJjZGVmZ2g="));
  EXPECT_EQ(*Answer(1)->FindStringByDottedPath("result.data"), "YWJjZGVmZ2g=");
}

}  // namespace
// Koan.captureScaled: the page's viewport, then a copy of the surface.
constexpr char kLayoutMetrics[] = R"({
    "cssVisualViewport": {"pageX": 0, "pageY": 0, "clientWidth": 1440,
                          "clientHeight": 900, "zoom": 1, "scale": 1}})";

TEST_F(KoanAgentFacadeTest, CaptureScaledCopiesTheSurfaceOfAUsersTab) {
  // A user's tab, taken over: the same capture as an agent's own tab.
  registry_.GrantUserTab("U1", "alice", TabState::kDriven);
  const std::string session = AttachTo("U1");
  ASSERT_FALSE(session.empty());
  Send(1, "Koan.captureScaled",
       Parse(R"({"targetId": "U1", "longEdge": 640, "scale": 1})"));
  // It asked the page for its viewport, nothing else (no emulation).
  ASSERT_FALSE(backend_.sent.empty());
  EXPECT_EQ(*backend_.sent.back().command.FindString("method"),
            "Page.getLayoutMetrics");
  AnswerLast(Parse(kLayoutMetrics));
  ASSERT_EQ(backend_.copies.size(), 1u);
  FakeBackend::Copy& copy = backend_.copies[0];
  EXPECT_EQ(copy.target_id, "U1");
  EXPECT_TRUE(copy.job.whole);
  EXPECT_EQ(copy.job.image_width, 640);
  EXPECT_EQ(copy.job.image_height, 400);
  EXPECT_TRUE(copy.job.masks.empty());
  EXPECT_FALSE(Answer(1));
  SurfaceCaptureResult copied;
  copied.status = SurfaceStatus::kOk;
  copied.encoded = {1, 2, 3};
  copied.width = 640;
  copied.height = 400;
  std::move(copy.done).Run(std::move(copied));
  const base::DictValue* answer = Answer(1);
  ASSERT_TRUE(answer);
  EXPECT_EQ(*answer->FindStringByDottedPath("result.data"), "AQID");
  EXPECT_EQ(answer->FindIntByDottedPath("result.width"), 640);
  EXPECT_EQ(answer->FindIntByDottedPath("result.height"), 400);
  EXPECT_EQ(answer->FindDoubleByDottedPath("result.cssPerPixel"), 2.25);
  EXPECT_EQ(*answer->FindStringByDottedPath("result.via"), "surface");
  for (const FakeBackend::Sent& sent : backend_.sent) {
    EXPECT_NE(*sent.command.FindString("method"), "Page.captureScreenshot");
  }
  ASSERT_FALSE(backend_.audits.empty());
  EXPECT_EQ(backend_.audits.back().event, "Koan.captureScaled");
  EXPECT_EQ(backend_.audits.back().outcome, AuditOutcome::kOk);

  // CDP's clip (device emulation, which resizes the view) stays refused on
  // the user's tab.
  Send(2, "Page.captureScreenshot",
       Parse(R"({"clip": {"x": 0, "y": 0, "width": 10, "height": 10,
                          "scale": 1}})"),
       session);
  EXPECT_EQ(ErrorCode(2), "Koan.notPermitted");
}

TEST_F(KoanAgentFacadeTest, CaptureScaledOnAReadOnlyTabAndItsErrors) {
  // A tab the agent may only read: a screenshot is a read.
  registry_.GrantUserTab("U1", "alice", TabState::kReadable);
  Send(1, "Koan.captureScaled", Parse(R"({"targetId": "U1"})"));
  EXPECT_EQ(ErrorCode(1), "Koan.notAttached");
  ASSERT_FALSE(AttachTo("U1").empty());
  Send(2, "Koan.captureScaled",
       Parse(R"({"targetId": "U1", "clip": {"x": 2000, "y": 0,
                                           "width": 50, "height": 50}})"));
  AnswerLast(Parse(kLayoutMetrics));
  EXPECT_EQ(ErrorCode(2), "Koan.offScreen");
  Send(3, "Koan.captureScaled",
       Parse(R"({"targetId": "U1", "clip": {"x": 10, "y": 10, "width": 0,
                                           "height": 0}})"));
  AnswerLast(Parse(kLayoutMetrics));
  EXPECT_EQ(ErrorCode(3), "Koan.emptyElement");
  Send(4, "Koan.captureScaled", Parse(R"({"targetId": "U1", "longEdge": 5})"));
  EXPECT_EQ(ErrorCode(4), "Koan.invalidParams");
  // Not the client's tab.
  Send(5, "Koan.captureScaled", Parse(R"({"targetId": "T2"})"));
  EXPECT_EQ(ErrorCode(5), "Koan.unknownTarget");
  EXPECT_TRUE(backend_.copies.empty());
}

TEST_F(KoanAgentFacadeTest, CaptureScaledFallsBackToCdpOnlyOnAgentTabs) {
  // An agent's own tab with no surface: CDP draws it, and the answer says so.
  const std::string session = AttachTo("T1");
  Send(1, "Koan.captureScaled",
       Parse(R"({"targetId": "T1", "longEdge": 320, "scale": 1})"));
  AnswerLast(Parse(kLayoutMetrics));
  ASSERT_EQ(backend_.copies.size(), 1u);
  SurfaceCaptureResult failed;
  failed.status = SurfaceStatus::kNoSurface;
  std::move(backend_.copies[0].done).Run(std::move(failed));
  const base::DictValue& fallback = backend_.sent.back().command;
  EXPECT_EQ(*fallback.FindString("method"), "Page.captureScreenshot");
  EXPECT_EQ(fallback.FindDoubleByDottedPath("params.clip.width"), 1440);
  EXPECT_NEAR(fallback.FindDoubleByDottedPath("params.clip.scale").value_or(0),
              320.0 / 2880, 1e-9);
  AnswerLast(Parse(R"({"data": "AQID"})"));
  const base::DictValue* answer = Answer(1);
  ASSERT_TRUE(answer);
  EXPECT_EQ(*answer->FindStringByDottedPath("result.via"), "cdp");
  EXPECT_EQ(*answer->FindStringByDottedPath("result.fallbackReason"),
            "noSurface");
  EXPECT_EQ(answer->FindIntByDottedPath("result.width"), 320);

  // A user's tab never: no emulation there, an error instead.
  registry_.GrantUserTab("U1", "alice", TabState::kDriven);
  ASSERT_FALSE(AttachTo("U1").empty());
  Send(2, "Koan.captureScaled", Parse(R"({"targetId": "U1"})"));
  AnswerLast(Parse(kLayoutMetrics));
  ASSERT_EQ(backend_.copies.size(), 2u);
  const size_t sent_before = backend_.sent.size();
  SurfaceCaptureResult none;
  none.status = SurfaceStatus::kNoSurface;
  std::move(backend_.copies[1].done).Run(std::move(none));
  EXPECT_EQ(ErrorCode(2), "Koan.noSurface");
  EXPECT_EQ(backend_.sent.size(), sent_before);
}

TEST_F(KoanAgentFacadeTest, CaptureScaledPaintsOverTheCardFieldsKoanFilled) {
  // A user's tab with a card Koan filled: one field in the page, one in a
  // payment frame in another process (its <iframe> is boxed).
  registry_.GrantUserTab("U1", "alice", TabState::kDriven);
  ASSERT_FALSE(AttachTo("U1").empty());
  backend_.facts.card_filled = true;
  backend_.card_fields.nodes = {41};
  backend_.card_fields.frames = {"FRAME1"};
  Send(1, "Koan.captureScaled",
       Parse(R"({"targetId": "U1", "longEdge": 640, "scale": 1,
                 "clip": {"x": 0, "y": 0, "width": 720, "height": 450}})"));
  AnswerLast(Parse(kLayoutMetrics));
  ASSERT_EQ(*backend_.sent.back().command.FindString("method"),
            "DOM.getFrameOwner");
  AnswerLast(Parse(R"({"backendNodeId": 50})"));
  const size_t sent = backend_.sent.size();
  ASSERT_GE(sent, 2u);
  // Field 41 at (90, 45) 180x18; the frame at (360, 225) 90x45; CSS px.
  const std::array<std::string_view, 2> quads = {
      R"({"quads": [[90, 45, 270, 45, 270, 63, 90, 63]]})",
      R"({"quads": [[360, 225, 450, 225, 450, 270, 360, 270]]})"};
  for (size_t i = 0; i < 2; ++i) {
    const base::DictValue& command = backend_.sent[sent - 2 + i].command;
    ASSERT_EQ(*command.FindString("method"), "DOM.getContentQuads");
    base::DictValue answer;
    answer.Set("id", *command.FindInt("id"));
    answer.Set("result", Parse(quads[i]));
    facade_.OnTabMessage("U1", *base::WriteJson(answer));
  }
  // The clip's 720x450 CSS px at 640 px: 1 image px = 1.125 CSS px, and
  // 2 CSS px of padding, rounded outwards.
  ASSERT_EQ(backend_.copies.size(), 1u);
  FakeBackend::Copy& copy = backend_.copies[0];
  ASSERT_EQ(copy.job.masks.size(), 2u);
  EXPECT_EQ(copy.job.masks[0], (CaptureRect{78, 38, 164, 20}));
  EXPECT_EQ(copy.job.masks[1], (CaptureRect{318, 198, 84, 44}));
  SurfaceCaptureResult copied;
  copied.status = SurfaceStatus::kOk;
  copied.encoded = {1, 2, 3};
  copied.width = 640;
  copied.height = 400;
  std::move(copy.done).Run(std::move(copied));
  EXPECT_EQ(Answer(1)->FindIntByDottedPath("result.masked"), 2);
  // Painted in the bitmap: nothing decoded and encoded again.
  EXPECT_TRUE(backend_.masks.empty());
  for (const FakeBackend::Sent& command : backend_.sent) {
    EXPECT_NE(*command.command.FindString("method"), "Page.captureScreenshot");
  }
}

TEST_F(KoanAgentFacadeTest, CaptureScaledIsRefusedWhenAFieldCannotBeFound) {
  ASSERT_FALSE(AttachTo("T1").empty());
  backend_.facts.card_filled = true;
  backend_.card_fields.frames = {"FRAME1"};
  Send(1, "Koan.captureScaled", Parse(R"({"targetId": "T1"})"));
  AnswerLast(Parse(kLayoutMetrics));
  base::DictValue error;
  error.Set("id", *backend_.sent.back().command.FindInt("id"));
  error.Set("error", base::DictValue().Set("code", -32000).Set("message", "x"));
  facade_.OnTabMessage("T1", *base::WriteJson(error));
  EXPECT_EQ(ErrorCode(1), "Koan.secretField");
  EXPECT_TRUE(backend_.copies.empty());
}

TEST_F(KoanAgentFacadeTest, CaptureScaledMasksTheCdpFallback) {
  // An agent's own tab with no surface and a card filled: CDP draws it as
  // PNG, and it is painted over and encoded as asked.
  ASSERT_FALSE(AttachTo("T1").empty());
  backend_.facts.card_filled = true;
  backend_.card_fields.nodes = {41};
  Send(1, "Koan.captureScaled",
       Parse(R"({"targetId": "T1", "longEdge": 640, "scale": 1,
                 "format": "jpeg", "quality": 60})"));
  AnswerLast(Parse(kLayoutMetrics));
  AnswerLast(Parse(R"({"quads": [[90, 45, 270, 45, 270, 63, 90, 63]]})"));
  ASSERT_EQ(backend_.copies.size(), 1u);
  ASSERT_EQ(backend_.copies[0].job.masks.size(), 1u);
  SurfaceCaptureResult failed;
  failed.status = SurfaceStatus::kNoSurface;
  std::move(backend_.copies[0].done).Run(std::move(failed));
  const base::DictValue& fallback = backend_.sent.back().command;
  ASSERT_EQ(*fallback.FindString("method"), "Page.captureScreenshot");
  EXPECT_EQ(*fallback.FindStringByDottedPath("params.format"), "png");
  EXPECT_FALSE(fallback.FindByDottedPath("params.quality"));
  AnswerLast(Parse(R"({"data": "iVBORw0K"})"));
  ASSERT_EQ(backend_.masks.size(), 1u);
  EXPECT_EQ(backend_.masked_format, "jpeg");
  ASSERT_EQ(backend_.masks[0].boxes.size(), 1u);
  EXPECT_DOUBLE_EQ(backend_.masks[0].boxes[0].x, 90);
  EXPECT_DOUBLE_EQ(backend_.masks[0].css_width, 1440);
  EXPECT_EQ(*Answer(1)->FindStringByDottedPath("result.data"),
            "masked:iVBORw0K");
  EXPECT_EQ(*Answer(1)->FindStringByDottedPath("result.via"), "cdp");
  EXPECT_EQ(Answer(1)->FindIntByDottedPath("result.masked"), 1);

  // Not hidden: no image at all.
  backend_.refuse_mask = true;
  Send(2, "Koan.captureScaled", Parse(R"({"targetId": "T1"})"));
  AnswerLast(Parse(kLayoutMetrics));
  AnswerLast(Parse(R"({"quads": [[90, 45, 270, 45, 270, 63, 90, 63]]})"));
  ASSERT_EQ(backend_.copies.size(), 2u);
  SurfaceCaptureResult none;
  none.status = SurfaceStatus::kNoSurface;
  std::move(backend_.copies[1].done).Run(std::move(none));
  AnswerLast(Parse(R"({"data": "iVBORw0K"})"));
  EXPECT_EQ(ErrorCode(2), "Koan.secretField");
}

TEST_F(KoanAgentFacadeTest, CaptureScaledEndsWithTheSession) {
  const std::string session = AttachTo("T1");
  Send(1, "Koan.captureScaled", Parse(R"({"targetId": "T1"})"));
  // The tab goes before it says how big its viewport is.
  facade_.OnTabDetached("T1");
  EXPECT_EQ(ErrorCode(1), "Koan.targetClosed");
}

}  // namespace koan::agent
