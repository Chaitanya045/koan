// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_DEVTOOLS_CLIENT_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_DEVTOOLS_CLIENT_H_

#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/devtools_agent_host_client.h"

namespace content {
class DevToolsAgentHost;
class WebContents;
}  // namespace content

// One connection's DevTools client on one tab (.agent/notes/agent-control.md
// sections 2 and 4.4): the chrome.debugger pattern, with every trust flag
// down. Untrusted, so the Browser, SystemInfo and Extensions domains and the
// browser target do not exist for it; no local files read or written; and it
// may attach only to frames koan_agent_reach.h allows, which content asks again
// for every navigation (a navigation to chrome:, file:, an extension page ...
// detaches it, and the facade tells the agent).
//
// It only carries bytes: the facade has judged a command before Send(), and
// judges every message it hands on. Commands run through the tab's keep-alive
// lease (AgentLeaseManager::RunCommand), so a hidden tab renders while the
// agent acts. UI thread.
namespace koan::agent {

class AgentDevToolsClient final : public content::DevToolsAgentHostClient {
 public:
  using MessageCallback = base::RepeatingCallback<void(std::string_view)>;

  // `on_message` gets every message from the tab; `on_closed` runs, from a
  // fresh task, once content detached the client by itself (the tab went away,
  // or navigated where the agent may not attach).
  AgentDevToolsClient(content::WebContents* contents,
                      MessageCallback on_message,
                      base::OnceClosure on_closed);
  AgentDevToolsClient(const AgentDevToolsClient&) = delete;
  AgentDevToolsClient& operator=(const AgentDevToolsClient&) = delete;
  ~AgentDevToolsClient() override;

  // Attaches to the tab's page target. False if content refused.
  bool Attach();
  bool attached() const { return static_cast<bool>(host_); }

  // Sends a command once the lease lets it run, if `may_dispatch` still says
  // yes then; otherwise runs `on_dropped` instead (the user paused the tab
  // while the command waited).
  void Send(std::string json,
            base::OnceCallback<bool()> may_dispatch,
            base::OnceClosure on_dropped);
  // A command sent with Send() was answered.
  void Answered();

  // content::DevToolsAgentHostClient:
  void DispatchProtocolMessage(content::DevToolsAgentHost* agent_host,
                               base::span<const uint8_t> message) override;
  void AgentHostClosed(content::DevToolsAgentHost* agent_host) override;
  bool MayAttachToURL(const GURL& url, bool is_webui) override;
  bool MayAttachToRenderFrameHost(
      content::RenderFrameHost* render_frame_host) override;
  bool IsTrusted() override;
  bool MayReadLocalFiles() override;
  bool MayWriteLocalFiles() override;
  bool AllowUnsafeOperations() override;
  std::string GetTypeForMetrics() override;

 private:
  void Dispatch(std::string json,
                base::OnceCallback<bool()> may_dispatch,
                base::OnceClosure on_dropped);
  void EndCommands();

  base::WeakPtr<content::WebContents> contents_;
  scoped_refptr<content::DevToolsAgentHost> host_;
  MessageCallback on_message_;
  base::OnceClosure on_closed_;
  // Commands begun on the lease and not answered: ended if the client goes.
  int in_flight_ = 0;
  base::WeakPtrFactory<AgentDevToolsClient> weak_factory_{this};
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_DEVTOOLS_CLIENT_H_
