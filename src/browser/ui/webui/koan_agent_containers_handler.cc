// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_agent_containers_handler.h"

#include <map>
#include <string>

#include "base/check_op.h"
#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_ui.h"
#include "koan/browser/containers/koan_agent_containers.h"
#include "koan/browser/containers/koan_container_grants.h"

namespace koan {

namespace {

const std::string* StringArg(const base::ListValue& args, size_t index) {
  return index < args.size() ? args[index].GetIfString() : nullptr;
}

}  // namespace

KoanAgentContainersHandler::KoanAgentContainersHandler(Profile& profile)
    : profile_(profile) {}

KoanAgentContainersHandler::~KoanAgentContainersHandler() = default;

void KoanAgentContainersHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "koanGetAgentContainers",
      base::BindRepeating(&KoanAgentContainersHandler::HandleGet,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "koanCreateAgentContainer",
      base::BindRepeating(&KoanAgentContainersHandler::HandleCreate,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "koanDeleteAgentContainer",
      base::BindRepeating(&KoanAgentContainersHandler::HandleDelete,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "koanSetAgentContainerGrant",
      base::BindRepeating(&KoanAgentContainersHandler::HandleSetGrant,
                          base::Unretained(this)));
}

void KoanAgentContainersHandler::OnJavascriptAllowed() {
  if (agent::AgentControlService* service = agent::AgentControlService::Get()) {
    observation_.Observe(service);
  }
}

void KoanAgentContainersHandler::OnJavascriptDisallowed() {
  observation_.Reset();
}

void KoanAgentContainersHandler::OnAgentControlChanged() {
  NotifyChanged();
}

void KoanAgentContainersHandler::HandleGet(const base::ListValue& args) {
  CHECK_EQ(args.size(), 1u);
  AllowJavascript();
  ResolveJavascriptCallback(args[0], AgentContainers());
}

void KoanAgentContainersHandler::HandleCreate(const base::ListValue& args) {
  CHECK_EQ(args.size(), 2u);
  AllowJavascript();
  const std::string* name = StringArg(args, 1);
  const bool ok =
      name && containers::CreateSavedAgentContainer(&*profile_, *name);
  ResolveJavascriptCallback(args[0], ok);
  if (ok) {
    NotifyChanged();
  }
}

void KoanAgentContainersHandler::HandleDelete(const base::ListValue& args) {
  CHECK_EQ(args.size(), 2u);
  AllowJavascript();
  if (const std::string* name = StringArg(args, 1)) {
    containers::DeleteSavedAgentContainer(&*profile_, *name);
  }
  ResolveJavascriptCallback(args[0], true);
  NotifyChanged();
}

void KoanAgentContainersHandler::HandleSetGrant(const base::ListValue& args) {
  CHECK_EQ(args.size(), 4u);
  AllowJavascript();
  const std::string* client_id = StringArg(args, 1);
  const std::string* container_id = StringArg(args, 2);
  const std::optional<bool> granted = args[3].GetIfBool();
  bool ok = false;
  if (client_id && container_id && granted) {
    // Only a saved container of this profile, and only a client the page
    // lists (a connected one or one that already holds a grant).
    bool known_container = false;
    for (const containers::SavedContainer& saved :
         containers::GetSavedAgentContainers(&*profile_)) {
      known_container |= saved.id.id == *container_id;
    }
    std::string label;
    bool known_client = false;
    for (const containers::ContainerGrants::Client& client :
         containers::ContainerGrants::Get().Clients()) {
      if (client.id == *client_id) {
        known_client = true;
        label = client.label;
      }
    }
    if (agent::AgentControlService* service =
            agent::AgentControlService::Get()) {
      for (const agent::ConnectedClient& client : service->clients()) {
        if (client.client_id == *client_id) {
          known_client = true;
          label = client.label;
        }
      }
    }
    ok = known_container && known_client;
    if (ok) {
      containers::ContainerGrants::Get().Set({.id = *client_id, .label = label},
                                             *container_id, *granted);
    }
  }
  ResolveJavascriptCallback(args[0], ok);
  NotifyChanged();
}

base::DictValue KoanAgentContainersHandler::AgentContainers() const {
  base::ListValue containers;
  for (const containers::SavedContainer& saved :
       containers::GetSavedAgentContainers(&*profile_)) {
    containers.Append(
        base::DictValue().Set("name", saved.name).Set("id", saved.id.id));
  }
  // id -> (label, connected)
  std::map<std::string, std::pair<std::string, bool>> clients;
  for (const containers::ContainerGrants::Client& client :
       containers::ContainerGrants::Get().Clients()) {
    clients[client.id] = {client.label, false};
  }
  if (agent::AgentControlService* service = agent::AgentControlService::Get()) {
    for (const agent::ConnectedClient& client : service->clients()) {
      clients[client.client_id] = {client.label, true};
    }
  }
  base::ListValue client_list;
  for (const auto& [id, client] : clients) {
    client_list.Append(base::DictValue()
                           .Set("id", id)
                           .Set("label", client.first)
                           .Set("connected", client.second));
  }
  base::ListValue grants;
  for (const auto& [client, container] :
       containers::ContainerGrants::Get().All()) {
    grants.Append(base::ListValue().Append(client).Append(container));
  }
  return base::DictValue()
      .Set("containers", std::move(containers))
      .Set("clients", std::move(client_list))
      .Set("grants", std::move(grants));
}

void KoanAgentContainersHandler::NotifyChanged() {
  if (IsJavascriptAllowed()) {
    FireWebUIListener("koan-agent-containers-changed", AgentContainers());
  }
}

}  // namespace koan
