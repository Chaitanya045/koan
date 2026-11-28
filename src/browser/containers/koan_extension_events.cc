// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/containers/koan_extension_events.h"

#include <optional>
#include <set>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/values.h"
#include "extensions/browser/event_router.h"
#include "extensions/common/extension.h"
#include "koan/browser/containers/koan_container_extensions.h"

namespace koan::containers {

namespace {

bool WillDispatchUnlessOff(
    const std::set<std::string>& off,
    const extensions::Event::WillDispatchCallback& next,
    content::BrowserContext* context,
    extensions::mojom::ContextType target_context,
    const extensions::Extension* extension,
    const base::DictValue* listener_filter,
    std::optional<base::ListValue>& event_args_out,
    extensions::mojom::EventFilteringInfoPtr& event_filtering_info_out,
    bool* dispatch_separate_event_out) {
  if (extension && off.contains(extension->id())) {
    return false;
  }
  return next.is_null() ||
         next.Run(context, target_context, extension, listener_filter,
                  event_args_out, event_filtering_info_out,
                  dispatch_separate_event_out);
}

}  // namespace

void SkipExtensionsOffIn(extensions::Event& event,
                         content::WebContents* contents) {
  std::set<std::string> off = ExtensionsOffInContents(contents);
  if (off.empty()) {
    return;
  }
  event.will_dispatch_callback =
      base::BindRepeating(&WillDispatchUnlessOff, std::move(off),
                          std::move(event.will_dispatch_callback));
}

}  // namespace koan::containers
