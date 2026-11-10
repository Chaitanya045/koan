// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_devtools_client.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/agent/koan_agent_lease.h"
#include "koan/browser/agent/koan_agent_reach.h"
#include "koan/browser/agent/koan_agent_reach_content.h"

namespace koan::agent {

namespace {

// A command the lease queued, run once the tab holds its capturer. If the
// client is gone by then, the command is ended at once so that the lease does
// not wait for an answer that never comes.
void RunQueued(base::WeakPtr<AgentDevToolsClient> client,
               base::WeakPtr<content::WebContents> contents,
               base::OnceClosure dispatch) {
  if (client) {
    std::move(dispatch).Run();
  } else if (contents) {
    AgentLeaseManager::Get().EndCommand(contents.get());
  }
}

}  // namespace

AgentDevToolsClient::AgentDevToolsClient(content::WebContents* contents,
                                         MessageCallback on_message,
                                         base::OnceClosure on_closed)
    : contents_(contents->GetWeakPtr()),
      on_message_(std::move(on_message)),
      on_closed_(std::move(on_closed)) {}

AgentDevToolsClient::~AgentDevToolsClient() {
  EndCommands();
  if (host_) {
    host_->DetachClient(this);
  }
}

bool AgentDevToolsClient::Attach() {
  if (host_ || !contents_) {
    return attached();
  }
  scoped_refptr<content::DevToolsAgentHost> host =
      content::DevToolsAgentHost::GetOrCreateFor(contents_.get());
  // AttachClient() asks MayAttachToRenderFrameHost() (and the browser's
  // DevTools policy) first.
  if (!host || !host->AttachClient(this)) {
    return false;
  }
  host_ = std::move(host);
  return true;
}

void AgentDevToolsClient::Send(std::string json) {
  if (!host_ || !contents_) {
    return;
  }
  ++in_flight_;
  AgentLeaseManager::Get().RunCommand(
      contents_.get(),
      base::BindOnce(
          &RunQueued, weak_factory_.GetWeakPtr(), contents_,
          base::BindOnce(&AgentDevToolsClient::Dispatch,
                         weak_factory_.GetWeakPtr(), std::move(json))));
}

void AgentDevToolsClient::Dispatch(std::string json) {
  if (!host_) {
    return;
  }
  host_->DispatchProtocolMessage(this, base::as_byte_span(json));
}

void AgentDevToolsClient::Answered() {
  if (in_flight_ == 0) {
    return;
  }
  --in_flight_;
  if (contents_) {
    AgentLeaseManager::Get().EndCommand(contents_.get());
  }
}

void AgentDevToolsClient::EndCommands() {
  for (; in_flight_ > 0; --in_flight_) {
    if (contents_) {
      AgentLeaseManager::Get().EndCommand(contents_.get());
    }
  }
}

void AgentDevToolsClient::DispatchProtocolMessage(
    content::DevToolsAgentHost* agent_host,
    base::span<const uint8_t> message) {
  if (agent_host != host_.get()) {
    return;
  }
  on_message_.Run(std::string_view(
      reinterpret_cast<const char*>(message.data()), message.size()));
}

void AgentDevToolsClient::AgentHostClosed(
    content::DevToolsAgentHost* agent_host) {
  if (agent_host != host_.get()) {
    return;
  }
  // Content has detached this client already; do not detach again.
  host_ = nullptr;
  EndCommands();
  // Not from inside content's call: the owner destroys this client.
  if (on_closed_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, std::move(on_closed_));
  }
}

bool AgentDevToolsClient::MayAttachToURL(const GURL& url, bool is_webui) {
  return koan::agent::MayAttachToUrl(url, is_webui);
}

bool AgentDevToolsClient::MayAttachToRenderFrameHost(
    content::RenderFrameHost* render_frame_host) {
  return MayReachRenderFrameHost(render_frame_host);
}

bool AgentDevToolsClient::IsTrusted() {
  return false;
}

bool AgentDevToolsClient::MayReadLocalFiles() {
  return false;
}

bool AgentDevToolsClient::MayWriteLocalFiles() {
  return false;
}

bool AgentDevToolsClient::AllowUnsafeOperations() {
  return false;
}

std::string AgentDevToolsClient::GetTypeForMetrics() {
  return "Other";
}

}  // namespace koan::agent
