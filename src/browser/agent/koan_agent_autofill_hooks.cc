// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_autofill_hooks.h"

#include <utility>

namespace koan::agent {

namespace {

AutofillHooksDelegate* g_delegate = nullptr;

}  // namespace

AutofillHooksDelegate::~AutofillHooksDelegate() = default;

void SetAutofillHooksDelegate(AutofillHooksDelegate* delegate) {
  g_delegate = delegate;
}

bool TakeAgentSuggestions(
    content::WebContents* contents,
    const autofill::AutofillClient::PopupOpenArgs& open_args,
    base::WeakPtr<autofill::AutofillSuggestionDelegate> delegate) {
  return g_delegate &&
         g_delegate->TakeSuggestions(contents, open_args, std::move(delegate));
}

bool RefusesAgentCrossDomainFill(content::WebContents* contents) {
  return g_delegate && g_delegate->RefusesCrossDomainFill(contents);
}

}  // namespace koan::agent
