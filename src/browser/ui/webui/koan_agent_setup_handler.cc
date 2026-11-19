// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/webui/koan_agent_setup_handler.h"

#include <stdlib.h>

#include "base/check_op.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "content/public/browser/web_ui.h"
#include "koan/browser/agent/koan_agent_setup.h"

#if BUILDFLAG(IS_MAC)
#include "base/apple/bundle_locations.h"
#endif

namespace koan {

namespace {

base::FilePath SetupHome() {
  const char* home = getenv("KOAN_AGENT_SETUP_HOME");
  return home && *home ? base::FilePath(home) : base::GetHomeDir();
}

// Koan.app/Contents/Resources/bin/koan (//koan/cli:koan_bundle_data); next
// to the browser elsewhere, as in an out dir.
base::FilePath ToolInThisApp() {
#if BUILDFLAG(IS_MAC)
  return base::apple::OuterBundlePath()
      .Append("Contents")
      .Append("Resources")
      .Append("bin")
      .Append("koan");
#else
  base::FilePath exe;
  base::PathService::Get(base::FILE_EXE, &exe);
  return exe.DirName().Append("koan");
#endif
}

}  // namespace

KoanAgentSetupHandler::KoanAgentSetupHandler()
    : home_(SetupHome()), cli_(ToolInThisApp()) {}

KoanAgentSetupHandler::~KoanAgentSetupHandler() = default;

void KoanAgentSetupHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "koanGetAgentSetup",
      base::BindRepeating(&KoanAgentSetupHandler::Handle,
                          base::Unretained(this), Action::kRead));
  web_ui()->RegisterMessageCallback(
      "koanSetUpAgents",
      base::BindRepeating(&KoanAgentSetupHandler::Handle,
                          base::Unretained(this), Action::kSetUp));
  web_ui()->RegisterMessageCallback(
      "koanRemoveAgentSetup",
      base::BindRepeating(&KoanAgentSetupHandler::Handle,
                          base::Unretained(this), Action::kRemove));
}

void KoanAgentSetupHandler::OnJavascriptDisallowed() {
  weak_factory_.InvalidateWeakPtrs();
}

void KoanAgentSetupHandler::Handle(Action action, const base::ListValue& args) {
  CHECK_EQ(args.size(), 1u);
  AllowJavascript();
  agent::AgentSetup (*work)(const base::FilePath&, const base::FilePath&) =
      action == Action::kSetUp    ? &agent::SetUpAgents
      : action == Action::kRemove ? &agent::RemoveAgentSetup
                                  : &agent::ReadAgentSetup;
  // USER_VISIBLE: the user clicked and waits for the answer.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce(work, home_, cli_),
      base::BindOnce(&KoanAgentSetupHandler::Reply, weak_factory_.GetWeakPtr(),
                     args[0].Clone()));
}

void KoanAgentSetupHandler::Reply(base::Value callback_id,
                                  const agent::AgentSetup& setup) {
  ResolveJavascriptCallback(callback_id,
                            agent::AgentSetupToValue(setup, home_));
}

}  // namespace koan
