// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_PROMPTS_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_PROMPTS_H_

#include <stddef.h>
#include <stdint.h>

#include <list>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"

// What a page asked to show while an agent holds its tab
// (.agent/notes/agent-control.md section 9.4), held back from the user's work
// until the user opens it from the agent indicator. Pure logic over closures:
// koan_agent_page_surfaces.h holds the real dialogs, sign-in requests and
// launches, and puts them here.
namespace koan::agent {

// The surfaces held. The order is not significant.
enum class PageSurface {
  // A beforeunload "Leave site?" dialog.
  kBeforeUnload,
  // alert(), confirm() or prompt().
  kJavaScriptDialog,
  // A link to an app (mailto:, zoommtg:, ...).
  kExternalProtocol,
  // An HTTP or proxy sign-in.
  kHttpAuth,
  // window.print().
  kPrint,
  // window.focus(), or a service worker's clients.focus().
  kFocus,
  // <input type=file> (also webkitdirectory).
  kFilePicker,
  // A WebUSB, WebHID, Web Serial or Web Bluetooth device chooser, or a
  // Bluetooth scanning prompt.
  kDeviceChooser,
  // getDisplayMedia()'s screen-share picker.
  kScreenShare,
};

// Whether `surface` only makes sense with the user in front of its tab (a
// picker): when the agent lets go of the tab it is cancelled, not shown.
bool NeedsUserInFront(PageSurface surface);

// The `reason` of the Koan.userNeeded event for `surface`.
std::string_view ReasonFor(PageSurface surface);

// The line the indicator shows for a held surface: `host` is the page's host
// (may be empty), `detail` the app link's scheme for kExternalProtocol.
std::u16string PromptTextFor(PageSurface surface,
                             std::string_view host,
                             std::string_view detail);

// A held surface, as the indicator and the agent see it.
struct UserPrompt {
  uint64_t id = 0;
  // An opaque key for the tab (the holder's own).
  uintptr_t tab = 0;
  PageSurface surface = PageSurface::kFocus;
  std::string host;
  std::string detail;
};

class PromptQueue {
 public:
  // A tab holds at most this many surfaces; the next ones are refused (a page
  // that launches app links in a loop does not grow the queue).
  static constexpr size_t kMaxPerTab = 4;

  // `replay` shows the surface the way Chrome would have (the holder brings
  // the tab forward first when the user opened it); `dismiss` answers it as
  // cancelled.
  struct Actions {
    base::OnceClosure replay;
    base::OnceClosure dismiss;
  };

  PromptQueue();
  PromptQueue(const PromptQueue&) = delete;
  PromptQueue& operator=(const PromptQueue&) = delete;
  ~PromptQueue();

  // Holds a surface and returns its id. Returns 0 if the tab holds a kFocus
  // already (`surface` is kFocus: one is enough) or is full; the caller then
  // answers the surface itself, and the actions are dropped unrun.
  uint64_t Add(uintptr_t tab,
               PageSurface surface,
               std::string host,
               std::string detail,
               Actions actions);

  // The user opened or dismissed the prompt: it leaves the queue, then its
  // replay or dismiss runs (which may call back into the queue). False if
  // there is no such prompt.
  bool Open(uint64_t id);
  bool Dismiss(uint64_t id);

  // The surface ended on its own (the page navigated, the agent answered the
  // dialog, the request went away): it leaves the queue and nothing runs.
  bool Forget(uint64_t id);
  // The tab closed: every prompt of it leaves the queue unrun.
  void ForgetTab(uintptr_t tab);
  // The agent let go of the tab: its prompts leave the queue and are replayed
  // in the order they came, except kFocus, which is dropped (it would take the
  // user's focus long after the page asked), and the pickers
  // (NeedsUserInFront()), which are dismissed (they would come up over the
  // user's work).
  void ReplayTab(uintptr_t tab);

  // Oldest first.
  std::vector<UserPrompt> List() const;
  const UserPrompt* Find(uint64_t id) const;
  bool empty() const { return entries_.empty(); }

 private:
  struct Entry {
    UserPrompt prompt;
    Actions actions;
  };

  // Takes the entry out of the queue; false if there is none.
  bool Take(uint64_t id, Entry* out);

  std::list<Entry> entries_;
  uint64_t next_id_ = 1;
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_PROMPTS_H_
