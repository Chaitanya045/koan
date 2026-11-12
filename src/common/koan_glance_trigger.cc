// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/common/koan_glance_trigger.h"

#include <atomic>

#include "base/command_line.h"

namespace koan {

namespace {

// The trigger the browser pushed last, or kNotSet. Written on the renderer's
// main thread, read there by blink's navigation policy.
constexpr int kNotSet = -1;
std::atomic<int> g_trigger{kNotSet};

}  // namespace

GlanceTrigger ParseGlanceTrigger(std::string_view method) {
  if (method == switches::kGlanceOff) {
    return GlanceTrigger::kOff;
  }
  if (method == "ctrl") {
    return GlanceTrigger::kCtrl;
  }
  if (method == "shift") {
    return GlanceTrigger::kShift;
  }
  if (method == "meta") {
    return GlanceTrigger::kMeta;
  }
  // Zen's default.
  return GlanceTrigger::kAlt;
}

bool IsGlanceTriggerClick(GlanceTrigger trigger,
                          bool ctrl,
                          bool shift,
                          bool alt,
                          bool meta) {
  if (ctrl + shift + alt + meta != 1) {
    return false;
  }
  switch (trigger) {
    case GlanceTrigger::kOff:
      return false;
    case GlanceTrigger::kAlt:
      return alt;
    case GlanceTrigger::kCtrl:
      return ctrl;
    case GlanceTrigger::kShift:
      return shift;
    case GlanceTrigger::kMeta:
      return meta;
  }
  return false;
}

GlanceTrigger GetGlanceTrigger() {
  const int pushed = g_trigger.load(std::memory_order_relaxed);
  if (pushed != kNotSet) {
    return static_cast<GlanceTrigger>(pushed);
  }
  return ParseGlanceTrigger(
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kGlanceActivationMethod));
}

void SetGlanceTrigger(GlanceTrigger trigger) {
  g_trigger.store(static_cast<int>(trigger), std::memory_order_relaxed);
}

}  // namespace koan
