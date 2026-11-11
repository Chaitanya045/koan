// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// The native approval prompt (.agent/notes/agent-control.md section 5.1): a
// browser-modal ui::DialogModel in the most recently active window. Its
// headline is the program the OS reported, never a name the client chose; the
// client's own label is shown only as "calls itself". No default button, and
// Esc, closing and the 60 s timeout (the session's, which cancels the dialog)
// all deny. It asks for attention (the Dock bounces) instead of activating the
// window: the user may be typing elsewhere.

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/dialogs/browser_dialogs.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/agent/koan_agent_auth.h"
#include "koan/browser/agent/koan_agent_service.h"
#include "ui/base/base_window.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/views/widget/widget.h"

namespace koan::agent {

namespace {

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kDenyButtonId);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kObserveId);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kReadId);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kDriveId);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kKeepAwakeId);

std::u16string Signer(const std::string& identity) {
  if (identity.empty()) {
    return u"unsigned";
  }
  if (identity == "apple") {
    return u"signed by Apple";
  }
  if (identity.starts_with("team:")) {
    return base::StrCat(
        {u"signed by team ", base::UTF8ToUTF16(identity.substr(5))});
  }
  return u"ad hoc signed";
}

std::u16string Describe(const ProcessInfo& process) {
  const base::FilePath path(process.path);
  return base::StrCat({base::UTF8ToUTF16(path.BaseName().value()), u" (",
                       Signer(process.signing_identity), u")"});
}

// One open dialog per request id, so the session's timeout can close it.
struct OpenDialog {
  // Owned by the dialog, which outlives every button's callback.
  raw_ptr<ui::DialogModel> model = nullptr;
  base::WeakPtr<views::Widget> widget;
  base::OnceCallback<void(ApprovalDecision)> answer;
};

std::map<uint64_t, OpenDialog>& OpenDialogs() {
  static base::NoDestructor<std::map<uint64_t, OpenDialog>> dialogs;
  return *dialogs;
}

void Answer(uint64_t id, ApprovalDecision decision) {
  auto it = OpenDialogs().find(id);
  if (it == OpenDialogs().end()) {
    return;
  }
  OpenDialog dialog = std::move(it->second);
  OpenDialogs().erase(it);
  if (dialog.answer) {
    std::move(dialog.answer).Run(decision);
  }
  if (dialog.widget && !dialog.widget->IsClosed()) {
    dialog.widget->Close();
  }
}

void Allow(uint64_t id, ClientCapabilities requested) {
  auto it = OpenDialogs().find(id);
  if (it == OpenDialogs().end() || !it->second.model) {
    return;
  }
  ui::DialogModel& model = *it->second.model;
  // Only the checkboxes that were added (one per capability asked for):
  // GetCheckboxByUniqueId() CHECKs that the field exists, so asking for one
  // that isn't there crashed Koan when the user pressed Allow.
  auto checked = [&](bool requested_it, ui::ElementIdentifier element) {
    return requested_it && model.GetCheckboxByUniqueId(element)->is_checked();
  };
  ClientCapabilities granted;
  granted.observe = checked(requested.observe, kObserveId);
  granted.read = checked(requested.read, kReadId);
  granted.drive = checked(requested.drive, kDriveId);
  granted.keep_awake = checked(requested.keep_awake, kKeepAwakeId);
  ApprovalDecision decision;
  decision.approved = true;
  decision.granted = Intersect(requested, granted);
  Answer(id, decision);
}

struct OpenTakeOver {
  base::WeakPtr<views::Widget> widget;
  base::OnceCallback<void(bool)> answer;
};

std::map<uint64_t, OpenTakeOver>& OpenTakeOvers() {
  static base::NoDestructor<std::map<uint64_t, OpenTakeOver>> dialogs;
  return *dialogs;
}

uint64_t g_next_take_over_id = 1;

void AnswerTakeOver(uint64_t id, bool allowed) {
  auto it = OpenTakeOvers().find(id);
  if (it == OpenTakeOvers().end()) {
    return;
  }
  OpenTakeOver dialog = std::move(it->second);
  OpenTakeOvers().erase(it);
  if (dialog.answer) {
    std::move(dialog.answer).Run(allowed);
  }
  if (dialog.widget && !dialog.widget->IsClosed()) {
    dialog.widget->Close();
  }
}

}  // namespace

uint64_t ShowTakeOverDialog(content::WebContents* contents,
                            const std::string& label,
                            const std::string& site,
                            base::OnceCallback<void(bool)> answer) {
  tabs::TabInterface* tab =
      contents ? tabs::TabInterface::MaybeGetFromContents(contents) : nullptr;
  BrowserWindowInterface* browser =
      tab ? tab->GetBrowserWindowInterface() : nullptr;
  const uint64_t id = g_next_take_over_id++;
  if (!browser) {
    std::move(answer).Run(false);
    return id;
  }
  const std::u16string agent =
      label.empty() ? u"An agent" : base::UTF8ToUTF16(label);
  ui::DialogModel::Builder builder;
  builder.SetTitle(base::StrCat({agent, u" wants to control a tab"}))
      .AddParagraph(ui::DialogModelLabel(base::StrCat(
          {u"“", contents->GetTitle(), u"” on ", base::UTF8ToUTF16(site)})))
      .AddParagraph(ui::DialogModelLabel(
          u"It can click and type in this tab while you are signed in, on "
          u"this site only. Using the tab yourself pauses it; leaving the "
          u"site ends its control."))
      .AddOkButton(base::BindOnce(&AnswerTakeOver, id, true),
                   ui::DialogModel::Button::Params().SetLabel(u"Allow"))
      .AddCancelButton(base::BindOnce(&AnswerTakeOver, id, false),
                       ui::DialogModel::Button::Params()
                           .SetLabel(u"Don’t Allow")
                           .SetId(kDenyButtonId))
      .SetCloseActionCallback(base::BindOnce(&AnswerTakeOver, id, false))
      .SetInitiallyFocusedField(kDenyButtonId)
      .OverrideDefaultButton(ui::mojom::DialogButton::kNone);
  OpenTakeOvers()[id] = OpenTakeOver{nullptr, std::move(answer)};
  views::Widget* widget = chrome::ShowBrowserModal(browser, builder.Build());
  auto it = OpenTakeOvers().find(id);
  if (it != OpenTakeOvers().end() && widget) {
    it->second.widget = widget->GetWeakPtr();
  }
  if (ui::BaseWindow* window = browser->GetWindow()) {
    window->FlashFrame(true);
  }
  return id;
}

void CancelTakeOverDialog(uint64_t dialog_id) {
  auto it = OpenTakeOvers().find(dialog_id);
  if (it == OpenTakeOvers().end()) {
    return;
  }
  it->second.answer.Reset();
  AnswerTakeOver(dialog_id, false);
}

void ShowApprovalDialog(const ApprovalRequest& request,
                        base::OnceCallback<void(ApprovalDecision)> answer) {
  BrowserWindowInterface* browser =
      GetLastActiveBrowserWindowInterfaceWithAnyProfile();
  if (!browser) {
    // No window to ask in: a denial. The client gets Koan.pairingDenied and
    // can retry once a window is open.
    std::move(answer).Run(ApprovalDecision());
    return;
  }

  std::u16string title;
  switch (request.kind) {
    case ApprovalKind::kNewClient:
      title = u"Allow a program to control Koan?";
      break;
    case ApprovalKind::kPeerChanged:
      title = u"A paired agent connects from a different program";
      break;
    case ApprovalKind::kChainChanged:
      title = u"A paired agent was started by a different program";
      break;
  }
  const ProcessInfo agent = AgentAncestor(request.peer);
  ui::DialogModel::Builder builder;
  builder.SetTitle(title).AddParagraph(ui::DialogModelLabel(
      base::StrCat({Describe(request.peer.process),
                    agent.path.empty()
                        ? std::u16string()
                        : base::StrCat({u", started by ", Describe(agent)})})));
  builder.AddParagraph(
      ui::DialogModelLabel(base::UTF8ToUTF16(request.peer.process.path)));
  if (!request.claimed_label.empty()) {
    builder.AddParagraph(ui::DialogModelLabel(
        base::StrCat({u"It calls itself “",
                      base::UTF8ToUTF16(request.claimed_label), u"”."})));
  }
  if (request.kind != ApprovalKind::kNewClient &&
      !request.previous.process.path.empty()) {
    builder.AddParagraph(ui::DialogModelLabel(
        base::StrCat({u"It was paired as ", Describe(request.previous.process),
                      request.previous.agent.path.empty()
                          ? std::u16string()
                          : base::StrCat({u", started by ",
                                          Describe(request.previous.agent)}),
                      u"."})));
  }
  builder.AddParagraph(ui::DialogModelLabel(
      u"It can open and work in its own background tabs, in a container "
      u"of their own: a throwaway one that is cleared when its task ends, "
      u"or one you saved for it. They never get your logins, cookies, "
      u"saved passwords or site permissions."));
  // The capabilities it asked for, on unless the user turns them off.
  struct Choice {
    bool requested;
    ui::ElementIdentifier id;
    const char16_t* label;
  };
  const Choice choices[] = {
      {request.requested.observe, kObserveId, u"Also list my tabs"},
      {request.requested.read, kReadId, u"Look at my current tab when it asks"},
      {request.requested.drive, kDriveId,
       u"Ask to control my tabs (each one asks again)"},
      {request.requested.keep_awake, kKeepAwakeId,
       u"Keep the display awake while it works"},
  };
  for (const Choice& choice : choices) {
    if (choice.requested) {
      builder.AddCheckbox(choice.id, ui::DialogModelLabel(choice.label),
                          ui::DialogModelCheckbox::Params().SetIsChecked(true));
    }
  }

  const uint64_t id = request.id;
  builder
      .AddOkButton(base::BindOnce(&Allow, id, request.requested),
                   ui::DialogModel::Button::Params().SetLabel(u"Allow"))
      .AddCancelButton(base::BindOnce(&Answer, id, ApprovalDecision()),
                       ui::DialogModel::Button::Params()
                           .SetLabel(u"Don’t Allow")
                           .SetId(kDenyButtonId))
      .SetCloseActionCallback(base::BindOnce(&Answer, id, ApprovalDecision()))
      .SetInitiallyFocusedField(kDenyButtonId)
      .OverrideDefaultButton(ui::mojom::DialogButton::kNone);
  std::unique_ptr<ui::DialogModel> model = builder.Build();
  OpenDialogs()[id] = OpenDialog{model.get(), nullptr, std::move(answer)};
  views::Widget* widget = chrome::ShowBrowserModal(browser, std::move(model));
  auto it = OpenDialogs().find(id);
  if (it != OpenDialogs().end() && widget) {
    it->second.widget = widget->GetWeakPtr();
  }
  if (ui::BaseWindow* window = browser->GetWindow()) {
    window->FlashFrame(true);
  }
}

void CancelApprovalDialog(uint64_t request_id) {
  auto it = OpenDialogs().find(request_id);
  if (it == OpenDialogs().end()) {
    return;
  }
  // The session gave up (its timeout, or the connection went): it no longer
  // waits for an answer.
  it->second.answer.Reset();
  Answer(request_id, ApprovalDecision());
}

}  // namespace koan::agent
