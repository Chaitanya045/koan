// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_router.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"

namespace koan::cli {

namespace {

// The facade's task when a command names none (CdpFacade::kDefaultTask).
constexpr char kDefaultTask[] = "Throwaway";

std::optional<base::DictValue> Parse(std::string_view json) {
  std::optional<base::Value> value =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!value || !value->is_dict()) {
    return std::nullopt;
  }
  return std::move(*value).TakeDict();
}

std::string Write(const base::DictValue& dict) {
  return base::WriteJson(dict).value_or("{}");
}

}  // namespace

Router::Router() = default;
Router::~Router() = default;

Router::Outgoing Router::FromCommand(ConnectionId connection,
                                     std::string_view json) {
  std::optional<base::DictValue> message = Parse(json);
  if (!message) {
    return {};
  }
  const std::optional<int> id = message->FindInt("id");
  const std::string* method = message->FindString("method");
  if (!id || !method) {
    return {};
  }
  Pending pending;
  pending.connection = connection;
  pending.command_id = *id;
  pending.method = *method;
  const base::DictValue* params = message->FindDict("params");
  if (*method == "Target.attachToTarget" && params) {
    const std::string* target = params->FindString("targetId");
    if (target) {
      if (auto it = sessions_.find(*target); it != sessions_.end()) {
        base::DictValue result;
        result.Set("sessionId", it->second);
        base::DictValue answer;
        answer.Set("id", *id);
        answer.Set("result", std::move(result));
        return {std::nullopt, Write(answer)};
      }
      pending.subject = *target;
    }
  } else if (*method == "Koan.openTab" || *method == "Koan.endTask") {
    const std::string* task = params ? params->FindString("task") : nullptr;
    const std::string* saved =
        params ? params->FindString("savedContainer") : nullptr;
    pending.subject = task && !task->empty()     ? *task
                      : saved && !saved->empty() ? *saved
                                                 : kDefaultTask;
  }
  const int browser_id = next_id_++;
  message->Set("id", browser_id);
  pending_[browser_id] = std::move(pending);
  return {Write(*message), std::nullopt};
}

std::vector<Router::Delivery> Router::FromBrowser(std::string_view json) {
  std::vector<Delivery> deliveries;
  std::optional<base::DictValue> message = Parse(json);
  if (!message) {
    return deliveries;
  }
  if (const std::optional<int> id = message->FindInt("id")) {
    auto it = pending_.find(*id);
    if (it == pending_.end()) {
      return deliveries;
    }
    Pending pending = std::move(it->second);
    pending_.erase(it);
    const base::DictValue* result = message->FindDict("result");
    if (result && pending.method == "Target.attachToTarget" &&
        !pending.subject.empty()) {
      if (const std::string* session = result->FindString("sessionId")) {
        sessions_[pending.subject] = *session;
      }
    } else if (result && pending.method == "Koan.openTab") {
      tasks_.insert(pending.subject);
    } else if (result && pending.method == "Koan.endTask") {
      tasks_.erase(pending.subject);
    }
    if (pending.connection) {
      message->Set("id", pending.command_id);
      deliveries.push_back({pending.connection, Write(*message)});
    }
    return deliveries;
  }
  const std::string* method = message->FindString("method");
  if (!method) {
    return deliveries;
  }
  // A tab's session ends when the tab goes or the browser detaches it.
  if (*method == "Target.detachedFromTarget" ||
      *method == "Target.targetDestroyed") {
    if (const base::DictValue* params = message->FindDict("params")) {
      if (const std::string* target = params->FindString("targetId")) {
        sessions_.erase(*target);
      }
      if (const std::string* session = params->FindString("sessionId")) {
        std::erase_if(sessions_, [&](const auto& entry) {
          return entry.second == *session;
        });
      }
    }
  }
  std::string text(json);
  // What status shows: the Koan.* events, not a page's own CDP traffic.
  if (method->starts_with("Koan.")) {
    events_.push_back(text);
    if (events_.size() > kMaxEvents) {
      events_.pop_front();
    }
  }
  // Watchers get the agent's own events and the tab list's, not every page
  // event of every session.
  if (method->starts_with("Koan.") || method->starts_with("Target.")) {
    for (ConnectionId watcher : watchers_) {
      deliveries.push_back({watcher, text});
    }
  }
  return deliveries;
}

void Router::AddWatcher(ConnectionId connection) {
  watchers_.insert(connection);
}

void Router::RemoveConnection(ConnectionId connection) {
  watchers_.erase(connection);
  // Its answers still come, and go nowhere.
  for (auto& [id, pending] : pending_) {
    if (pending.connection == connection) {
      pending.connection = 0;
    }
  }
}

}  // namespace koan::cli
