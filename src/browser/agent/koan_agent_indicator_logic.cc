// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_indicator_logic.h"

#include <algorithm>
#include <set>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"

namespace koan::agent {

namespace {

constexpr char16_t kDot[] = u"● ";

std::u16string AgentName(const std::string& label) {
  return label.empty() ? u"The agent" : base::UTF8ToUTF16(label);
}

// A connected client's name, before anything controls a tab.
std::u16string ClientName(const std::string& label) {
  return label.empty() ? u"An agent" : base::UTF8ToUTF16(label);
}

// The distinct clients, in the order they connected.
std::vector<const IndicatorClient*> Distinct(
    const std::vector<IndicatorClient>& clients) {
  std::vector<const IndicatorClient*> distinct;
  for (const IndicatorClient& client : clients) {
    if (std::ranges::none_of(distinct, [&](const IndicatorClient* seen) {
          return seen->client_id == client.client_id;
        })) {
      distinct.push_back(&client);
    }
  }
  return distinct;
}

// "Claude Code is connected", "2 agents connected".
std::u16string ConnectedPhrase(
    const std::vector<const IndicatorClient*>& distinct) {
  if (distinct.size() > 1) {
    return base::StrCat(
        {base::NumberToString16(distinct.size()), u" agents connected"});
  }
  return base::StrCat({ClientName(distinct.front()->label), u" is connected"});
}

PillContent PromptPill(const IndicatorPrompt& prompt, size_t others) {
  PillContent pill;
  pill.prompt_id = prompt.prompt.id;
  pill.text = base::StrCat(
      {kDot, PromptTextFor(prompt.prompt.surface, prompt.prompt.host,
                           prompt.prompt.detail)});
  if (others) {
    base::StrAppend(&pill.text, {u" (+", base::NumberToString16(others), u")"});
  }
  return pill;
}

PillContent TabPill(const IndicatorTab& tab) {
  PillContent pill;
  pill.target_id = tab.target_id;
  const std::u16string agent = AgentName(tab.label);
  pill.take_back = !tab.agent_owned;
  if (tab.state == TabState::kPaused) {
    pill.hand_back = true;
    pill.text =
        base::StrCat({kDot, agent,
                      tab.resume_asked ? u" asks to continue in this tab"
                                       : u" paused: you used this tab"});
    return pill;
  }
  // An agent's own tab is paused by taking it over; a user tab is taken
  // back.
  pill.take_over = tab.agent_owned && tab.state == TabState::kDriven;
  pill.text = base::StrCat({kDot, agent,
                            tab.state == TabState::kReadable
                                ? u" can read this tab"
                                : u" is controlling this tab"});
  return pill;
}

}  // namespace

IndicatorContent IndicatorFor(const std::vector<IndicatorClient>& clients,
                              const std::vector<IndicatorTab>& tabs,
                              const std::vector<IndicatorPrompt>& prompts) {
  IndicatorContent content;
  const std::vector<const IndicatorClient*> distinct = Distinct(clients);
  if (distinct.empty()) {
    return content;
  }
  content.connected = true;

  // The pill: a page waiting in the shown tab first (oldest first: the
  // rest follow as each is answered), else the shown tab itself.
  const auto shown_prompt =
      std::ranges::find_if(prompts, &IndicatorPrompt::shown);
  if (shown_prompt != prompts.end()) {
    const size_t others =
        std::ranges::count_if(prompts, [&](const IndicatorPrompt& prompt) {
          return prompt.shown && prompt.prompt.tab == shown_prompt->prompt.tab;
        });
    content.pill = PromptPill(*shown_prompt, others - 1);
  } else {
    // In a split, a paused pane first: the user has something to decide.
    const IndicatorTab* pill_tab = nullptr;
    for (const IndicatorTab& tab : tabs) {
      if (!tab.in_window || !tab.shown) {
        continue;
      }
      if (!pill_tab || (tab.state == TabState::kPaused &&
                        pill_tab->state != TabState::kPaused)) {
        pill_tab = &tab;
      }
    }
    if (pill_tab) {
      content.pill = TabPill(*pill_tab);
    }
  }

  // The chip, and what waits for the user elsewhere: prompts of tabs not
  // shown here, one per tab, and paused tabs without one.
  std::set<uintptr_t> prompt_tabs;
  for (const IndicatorPrompt& prompt : prompts) {
    if (!prompt.shown) {
      prompt_tabs.insert(prompt.prompt.tab);
    }
  }
  const size_t paused = std::ranges::count_if(tabs, [](const IndicatorTab& t) {
    return t.state == TabState::kPaused && !(t.in_window && t.shown);
  });
  // (A held prompt does not pause its tab, so the two rarely overlap.)
  ChipContent& chip = content.chip;
  chip.waiting = prompt_tabs.size() + paused;
  if (distinct.size() > 1) {
    chip.text =
        base::StrCat({base::NumberToString16(distinct.size()), u" agents"});
  } else {
    chip.text = distinct.front()->label.empty()
                    ? u"Agent"
                    : base::UTF8ToUTF16(distinct.front()->label);
  }
  chip.accessible_name = ConnectedPhrase(distinct);
  if (chip.waiting) {
    const std::u16string count = base::NumberToString16(chip.waiting);
    base::StrAppend(&chip.text, {u" · ", count, u" waiting"});
    base::StrAppend(
        &chip.accessible_name,
        {u"; ", count, chip.waiting == 1 ? u" tab waits" : u" tabs wait",
         u" for you"});
  }
  return content;
}

std::vector<MenuEntry> MenuFor(const std::vector<IndicatorTab>& tabs,
                               const std::vector<IndicatorPrompt>& prompts) {
  std::vector<MenuEntry> entries;
  std::set<uintptr_t> prompt_tabs;
  for (const IndicatorPrompt& prompt : prompts) {
    if (prompt.shown || !prompt_tabs.insert(prompt.prompt.tab).second) {
      continue;
    }
    entries.push_back(
        {.label = PromptTextFor(prompt.prompt.surface, prompt.prompt.host,
                                prompt.prompt.detail),
         .prompt_id = prompt.prompt.id});
  }
  for (const bool paused : {true, false}) {
    for (const IndicatorTab& tab : tabs) {
      if ((tab.state == TabState::kPaused) != paused ||
          (tab.in_window && tab.shown)) {
        continue;
      }
      const std::u16string title = tab.title.empty() ? u"Untitled" : tab.title;
      entries.push_back(
          {.label = base::StrCat(
               {AgentName(tab.label), paused ? u" (paused): " : u": ", title}),
           .target_id = tab.target_id});
    }
  }
  return entries;
}

EventState EventStateFor(const std::vector<IndicatorClient>& clients,
                         const std::vector<IndicatorTab>& tabs) {
  EventState state;
  for (const IndicatorClient& client : clients) {
    state.clients.emplace(client.client_id, client.label);
  }
  for (const IndicatorTab& tab : tabs) {
    if (tab.in_window && tab.state == TabState::kPaused) {
      state.paused.emplace(tab.target_id, tab.label);
    }
  }
  return state;
}

EventToasts EventToastsFor(const EventState& before, const EventState& after) {
  EventToasts toasts;
  for (const auto& [id, label] : after.clients) {
    if (!before.clients.contains(id)) {
      toasts.connection = base::StrCat({ClientName(label), u" connected"});
      break;
    }
  }
  if (toasts.connection.empty()) {
    for (const auto& [id, label] : before.clients) {
      if (!after.clients.contains(id)) {
        toasts.connection = base::StrCat({ClientName(label), u" disconnected"});
        break;
      }
    }
  }
  for (const auto& [id, label] : after.paused) {
    if (!before.paused.contains(id)) {
      toasts.pause =
          base::StrCat({AgentName(label), u" paused: you used its tab"});
      break;
    }
  }
  return toasts;
}

}  // namespace koan::agent
