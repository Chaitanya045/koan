// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_command_registry.h"

#include <algorithm>
#include <utility>

namespace koan {

KoanCommand::KoanCommand() = default;
KoanCommand::KoanCommand(const KoanCommand&) = default;
KoanCommand& KoanCommand::operator=(const KoanCommand&) = default;
KoanCommand::KoanCommand(KoanCommand&&) = default;
KoanCommand& KoanCommand::operator=(KoanCommand&&) = default;
KoanCommand::~KoanCommand() = default;

KoanCommandRegistry::KoanCommandRegistry() = default;
KoanCommandRegistry::~KoanCommandRegistry() = default;

void KoanCommandRegistry::AddSource(Source source) {
  sources_.push_back(std::move(source));
}

std::vector<KoanCommand> KoanCommandRegistry::GetAvailableCommands() const {
  std::vector<KoanCommand> commands;
  for (const Source& source : sources_) {
    source.Run(commands);
  }
  std::ranges::stable_sort(commands, {}, &KoanCommand::order);
  return commands;
}

}  // namespace koan
