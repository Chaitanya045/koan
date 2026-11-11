// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_prompts.h"

#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {
namespace {

class KoanAgentPromptsTest : public testing::Test {
 protected:
  // Actions that record "replay:<name>" or "dismiss:<name>".
  PromptQueue::Actions Record(const std::string& name) {
    PromptQueue::Actions actions;
    actions.replay = base::BindOnce(
        [](std::vector<std::string>* log, std::string name) {
          log->push_back("replay:" + name);
        },
        &log_, name);
    actions.dismiss = base::BindOnce(
        [](std::vector<std::string>* log, std::string name) {
          log->push_back("dismiss:" + name);
        },
        &log_, name);
    return actions;
  }

  PromptQueue queue_;
  std::vector<std::string> log_;
};

TEST_F(KoanAgentPromptsTest, OpenReplaysAndDismissCancelsOnce) {
  const uint64_t a =
      queue_.Add(1, PageSurface::kJavaScriptDialog, "a.test", "", Record("a"));
  const uint64_t b =
      queue_.Add(1, PageSurface::kPrint, "a.test", "", Record("b"));
  ASSERT_NE(a, 0u);
  ASSERT_NE(b, 0u);
  EXPECT_EQ(queue_.List().size(), 2u);

  EXPECT_TRUE(queue_.Open(a));
  EXPECT_TRUE(queue_.Dismiss(b));
  EXPECT_EQ(log_, (std::vector<std::string>{"replay:a", "dismiss:b"}));
  // Each runs once.
  EXPECT_FALSE(queue_.Open(a));
  EXPECT_FALSE(queue_.Dismiss(b));
  EXPECT_TRUE(queue_.empty());
}

TEST_F(KoanAgentPromptsTest, ForgetRunsNothing) {
  const uint64_t a =
      queue_.Add(1, PageSurface::kBeforeUnload, "", "", Record("a"));
  queue_.Add(2, PageSurface::kHttpAuth, "", "", Record("b"));
  queue_.Add(2, PageSurface::kPrint, "", "", Record("c"));
  EXPECT_TRUE(queue_.Forget(a));
  queue_.ForgetTab(2);
  EXPECT_TRUE(queue_.empty());
  EXPECT_TRUE(log_.empty());
}

TEST_F(KoanAgentPromptsTest, OneFocusPerTabAndACapPerTab) {
  EXPECT_NE(queue_.Add(1, PageSurface::kFocus, "", "", Record("f1")), 0u);
  EXPECT_EQ(queue_.Add(1, PageSurface::kFocus, "", "", Record("f2")), 0u);
  // Another tab has its own.
  EXPECT_NE(queue_.Add(2, PageSurface::kFocus, "", "", Record("f3")), 0u);

  for (size_t i = 1; i < PromptQueue::kMaxPerTab; ++i) {
    EXPECT_NE(queue_.Add(1, PageSurface::kExternalProtocol, "", "mailto",
                         Record("x")),
              0u);
  }
  EXPECT_EQ(queue_.Add(1, PageSurface::kExternalProtocol, "", "mailto",
                       Record("over")),
            0u);
  // A refused surface's actions never run: the caller answers it.
  EXPECT_TRUE(log_.empty());
}

TEST_F(KoanAgentPromptsTest, ReplayTabReplaysInOrderAndDropsFocus) {
  queue_.Add(1, PageSurface::kJavaScriptDialog, "", "", Record("a"));
  queue_.Add(1, PageSurface::kFocus, "", "", Record("focus"));
  queue_.Add(2, PageSurface::kPrint, "", "", Record("other"));
  queue_.Add(1, PageSurface::kHttpAuth, "", "", Record("b"));
  queue_.ReplayTab(1);
  EXPECT_EQ(log_, (std::vector<std::string>{"replay:a", "replay:b"}));
  ASSERT_EQ(queue_.List().size(), 1u);
  EXPECT_EQ(queue_.List()[0].tab, 2u);
}

TEST_F(KoanAgentPromptsTest, ReplayTabDismissesPickers) {
  // A picker shown after the agent let go would come up over the user's work.
  queue_.Add(1, PageSurface::kFilePicker, "", "", Record("file"));
  queue_.Add(1, PageSurface::kJavaScriptDialog, "", "", Record("a"));
  queue_.Add(1, PageSurface::kDeviceChooser, "", "", Record("device"));
  queue_.Add(1, PageSurface::kScreenShare, "", "", Record("screen"));
  queue_.Add(2, PageSurface::kPasskey, "", "", Record("passkey"));
  queue_.ReplayTab(1);
  queue_.ReplayTab(2);
  EXPECT_EQ(log_, (std::vector<std::string>{"dismiss:file", "replay:a",
                                            "dismiss:device", "dismiss:screen",
                                            "dismiss:passkey"}));
  EXPECT_TRUE(queue_.empty());
}

TEST_F(KoanAgentPromptsTest, AReplayMayCallBackIntoTheQueue) {
  // Showing a dialog can make the page cancel another one (it navigates).
  uint64_t second = 0;
  PromptQueue::Actions first;
  first.replay = base::BindOnce(
      [](PromptQueue* queue, uint64_t* id) { EXPECT_TRUE(queue->Forget(*id)); },
      &queue_, &second);
  first.dismiss = base::DoNothing();
  const uint64_t id =
      queue_.Add(1, PageSurface::kJavaScriptDialog, "", "", std::move(first));
  second = queue_.Add(1, PageSurface::kJavaScriptDialog, "", "", Record("b"));
  EXPECT_TRUE(queue_.Open(id));
  EXPECT_TRUE(queue_.empty());
  EXPECT_TRUE(log_.empty());
}

TEST_F(KoanAgentPromptsTest, ReasonsAndText) {
  EXPECT_EQ(ReasonFor(PageSurface::kBeforeUnload), "beforeunload");
  EXPECT_EQ(ReasonFor(PageSurface::kExternalProtocol), "external-protocol");
  EXPECT_EQ(ReasonFor(PageSurface::kHttpAuth), "http-auth");
  EXPECT_EQ(ReasonFor(PageSurface::kFilePicker), "file-picker");
  EXPECT_EQ(ReasonFor(PageSurface::kDeviceChooser), "device-chooser");
  EXPECT_EQ(ReasonFor(PageSurface::kScreenShare), "screen-share");
  EXPECT_EQ(ReasonFor(PageSurface::kPasskey), "passkey");
  EXPECT_EQ(PromptTextFor(PageSurface::kExternalProtocol, "a.test", "mailto"),
            u"a.test wants to open a mailto: link");
  EXPECT_EQ(PromptTextFor(PageSurface::kPrint, "", ""),
            u"A page wants to print");
}

}  // namespace
}  // namespace koan::agent
