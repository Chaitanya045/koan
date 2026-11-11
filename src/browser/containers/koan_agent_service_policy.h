// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_CONTAINERS_KOAN_AGENT_SERVICE_POLICY_H_
#define KOAN_BROWSER_CONTAINERS_KOAN_AGENT_SERVICE_POLICY_H_

#include <string_view>

#include "base/containers/span.h"

// What each per-profile KeyedService means for agent containers
// (.agent/notes/agent-control.md section 7.2). A container is a storage
// partition inside the user's profile, so it shares every service of the
// profile; a service that could carry agent pages' data into the user's
// surfaces, or the user's data to agent pages, must be blocked for agent
// containers. Every factory is listed by its name
// (KeyedServiceBaseFactory::name()), so that a Chromium roll that adds one
// fails koan_agent_service_unittests (at runtime) and
// scripts/agent/check_agent_services.py (CI, statically) until someone decides
// it.
namespace koan::containers {

enum class ServiceVerdict {
  // Nothing of an agent tab reaches the user's data through it, or it only
  // gets tab data through a surface that is blocked (the reason says which).
  kSafe,
  // One of the agent blocks makes it unreachable from agent tabs (the reason
  // names the block, B1-B9). The extensions, password, autofill and history
  // blocks (B3, B4, B5, B7) apply while the user's "What agents can use"
  // switch for them is off (koan_agent_access.h): with it on, the service is
  // the user's in agent tabs, by the user's choice.
  kBlocked,
  // It can still carry data between agent tabs and the user's profile, and
  // no block covers it yet: a known gap, tracked in the design.
  kGap,
};

struct ServiceRule {
  std::string_view name;
  ServiceVerdict verdict;
  std::string_view reason;
};

// Every rule, sorted by name.
base::span<const ServiceRule> AgentServiceRules();

// The rule for the factory named `name`, or null.
const ServiceRule* FindAgentServiceRule(std::string_view name);

}  // namespace koan::containers

#endif  // KOAN_BROWSER_CONTAINERS_KOAN_AGENT_SERVICE_POLICY_H_
