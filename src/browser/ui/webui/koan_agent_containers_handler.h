// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_WEBUI_KOAN_AGENT_CONTAINERS_HANDLER_H_
#define KOAN_BROWSER_UI_WEBUI_KOAN_AGENT_CONTAINERS_HANDLER_H_

#include "base/memory/raw_ref.h"
#include "base/scoped_observation.h"
#include "base/values.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "koan/browser/agent/koan_agent_service.h"

class Profile;

namespace koan {

// Settings > Agent Control > Saved containers (koan_agent_containers.h,
// koan_container_grants.h). Messages from the page:
//   koanGetAgentContainers()           -> AgentContainers()
//   koanCreateAgentContainer(name)     -> bool
//   koanDeleteAgentContainer(name)
//   koanSetAgentContainerGrant(client, container, granted)
// and the event "koan-agent-containers-changed" (AgentContainers()) when a
// client connects or disconnects. The clients listed are the connected ones
// and the ones holding a grant; grants are in memory, like the pairings.
class KoanAgentContainersHandler : public content::WebUIMessageHandler,
                                   public agent::AgentControlService::Observer {
 public:
  // `profile`: the regular profile whose saved containers are shown.
  explicit KoanAgentContainersHandler(Profile& profile);
  KoanAgentContainersHandler(const KoanAgentContainersHandler&) = delete;
  KoanAgentContainersHandler& operator=(const KoanAgentContainersHandler&) =
      delete;
  ~KoanAgentContainersHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // agent::AgentControlService::Observer:
  void OnAgentControlChanged() override;

 private:
  void HandleGet(const base::ListValue& args);
  void HandleCreate(const base::ListValue& args);
  void HandleDelete(const base::ListValue& args);
  void HandleSetGrant(const base::ListValue& args);

  // {containers: [{name, id}], clients: [{id, label, connected}],
  //  grants: [[client id, container id]]}
  base::DictValue AgentContainers() const;
  void NotifyChanged();

  const raw_ref<Profile> profile_;
  base::ScopedObservation<agent::AgentControlService,
                          agent::AgentControlService::Observer>
      observation_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_WEBUI_KOAN_AGENT_CONTAINERS_HANDLER_H_
