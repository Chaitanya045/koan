// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_prompts.h"

#include <algorithm>
#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"

namespace koan::agent {

std::string_view ReasonFor(PageSurface surface) {
  switch (surface) {
    case PageSurface::kBeforeUnload:
      return "beforeunload";
    case PageSurface::kJavaScriptDialog:
      return "dialog";
    case PageSurface::kExternalProtocol:
      return "external-protocol";
    case PageSurface::kHttpAuth:
      return "http-auth";
    case PageSurface::kPrint:
      return "print";
    case PageSurface::kFocus:
      return "focus";
  }
  return "";
}

std::u16string PromptTextFor(PageSurface surface,
                             std::string_view host,
                             std::string_view detail) {
  const std::u16string page =
      host.empty() ? u"A page" : base::UTF8ToUTF16(host);
  switch (surface) {
    case PageSurface::kBeforeUnload:
      return base::StrCat({page, u" asks before you leave it"});
    case PageSurface::kJavaScriptDialog:
      return base::StrCat({page, u" shows a message"});
    case PageSurface::kExternalProtocol:
      return detail.empty()
                 ? base::StrCat({page, u" wants to open an app"})
                 : base::StrCat({page, u" wants to open a ",
                                 base::UTF8ToUTF16(detail), u": link"});
    case PageSurface::kHttpAuth:
      return base::StrCat({page, u" asks you to sign in"});
    case PageSurface::kPrint:
      return base::StrCat({page, u" wants to print"});
    case PageSurface::kFocus:
      return base::StrCat({page, u" wants your attention"});
  }
  return page;
}

PromptQueue::PromptQueue() = default;
PromptQueue::~PromptQueue() = default;

uint64_t PromptQueue::Add(uintptr_t tab,
                          PageSurface surface,
                          std::string host,
                          std::string detail,
                          Actions actions) {
  size_t count = 0;
  for (const Entry& entry : entries_) {
    if (entry.prompt.tab != tab) {
      continue;
    }
    if (surface == PageSurface::kFocus &&
        entry.prompt.surface == PageSurface::kFocus) {
      return 0;
    }
    ++count;
  }
  if (count >= kMaxPerTab) {
    return 0;
  }
  Entry entry;
  entry.prompt.id = next_id_++;
  entry.prompt.tab = tab;
  entry.prompt.surface = surface;
  entry.prompt.host = std::move(host);
  entry.prompt.detail = std::move(detail);
  entry.actions = std::move(actions);
  const uint64_t id = entry.prompt.id;
  entries_.push_back(std::move(entry));
  return id;
}

bool PromptQueue::Take(uint64_t id, Entry* out) {
  auto it = std::ranges::find(entries_, id,
                              [](const Entry& e) { return e.prompt.id; });
  if (it == entries_.end()) {
    return false;
  }
  *out = std::move(*it);
  entries_.erase(it);
  return true;
}

bool PromptQueue::Open(uint64_t id) {
  Entry entry;
  if (!Take(id, &entry)) {
    return false;
  }
  if (entry.actions.replay) {
    std::move(entry.actions.replay).Run();
  }
  return true;
}

bool PromptQueue::Dismiss(uint64_t id) {
  Entry entry;
  if (!Take(id, &entry)) {
    return false;
  }
  if (entry.actions.dismiss) {
    std::move(entry.actions.dismiss).Run();
  }
  return true;
}

bool PromptQueue::Forget(uint64_t id) {
  Entry entry;
  return Take(id, &entry);
}

void PromptQueue::ForgetTab(uintptr_t tab) {
  std::erase_if(entries_,
                [tab](const Entry& e) { return e.prompt.tab == tab; });
}

void PromptQueue::ReplayTab(uintptr_t tab) {
  std::list<Entry> taken;
  for (auto it = entries_.begin(); it != entries_.end();) {
    auto next = std::next(it);
    if (it->prompt.tab == tab) {
      taken.splice(taken.end(), entries_, it);
    }
    it = next;
  }
  // Out of the queue first: a replay may add to it or forget from it.
  for (Entry& entry : taken) {
    if (entry.prompt.surface != PageSurface::kFocus && entry.actions.replay) {
      std::move(entry.actions.replay).Run();
    }
  }
}

std::vector<UserPrompt> PromptQueue::List() const {
  std::vector<UserPrompt> prompts;
  prompts.reserve(entries_.size());
  for (const Entry& entry : entries_) {
    prompts.push_back(entry.prompt);
  }
  return prompts;
}

const UserPrompt* PromptQueue::Find(uint64_t id) const {
  auto it = std::ranges::find(entries_, id,
                              [](const Entry& e) { return e.prompt.id; });
  return it == entries_.end() ? nullptr : &it->prompt;
}

}  // namespace koan::agent
