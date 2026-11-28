// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_UI_WEBUI_KOAN_AGENT_SETUP_HANDLER_H_
#define KOAN_BROWSER_UI_WEBUI_KOAN_AGENT_SETUP_HANDLER_H_

#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/web_ui_message_handler.h"

namespace koan {

// Settings > Agent Control > "Set up agents on this Mac" (this computer on
// Linux; koan_agent_setup.h). Messages from the page, each answered with
// AgentSetupToValue() and the machine's name ("machine") once the disk work,
// done off the UI thread, is over:
//   koanGetAgentSetup()     what is set up now
//   koanSetUpAgents()       link ~/.local/bin/koan, write the skills
//   koanRemoveAgentSetup()  undo it
// Nothing is written but on the user's click (design decision 10). In a
// Flatpak (Linux) nothing is written at all: the answer says "sandboxed"
// and gives the command the user runs once in a terminal ("setupCommand").
class KoanAgentSetupHandler : public content::WebUIMessageHandler {
 public:
  KoanAgentSetupHandler();
  KoanAgentSetupHandler(const KoanAgentSetupHandler&) = delete;
  KoanAgentSetupHandler& operator=(const KoanAgentSetupHandler&) = delete;
  ~KoanAgentSetupHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptDisallowed() override;

 private:
  enum class Action { kRead, kSetUp, kRemove };
  void Handle(Action action, const base::ListValue& args);
  void Reply(base::Value callback_id, base::DictValue setup);

  // The home directory the setup writes under ($KOAN_AGENT_SETUP_HOME for
  // the Mac tests, which never touch the user's), and the `koan` tool of
  // this app.
  const base::FilePath home_;
  const base::FilePath cli_;
  base::WeakPtrFactory<KoanAgentSetupHandler> weak_factory_{this};
};

}  // namespace koan

#endif  // KOAN_BROWSER_UI_WEBUI_KOAN_AGENT_SETUP_HANDLER_H_
