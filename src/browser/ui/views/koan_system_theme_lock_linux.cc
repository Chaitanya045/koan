// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_system_theme_lock_linux.h"

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service_factory.h"

namespace koan {

KoanSystemThemeLock::KoanSystemThemeLock(Profile* profile) {
  if (!profile) {
    return;
  }
  theme_service_ =
      ThemeServiceFactory::GetForProfile(profile->GetOriginalProfile());
  if (!theme_service_) {
    return;
  }
  observation_.Observe(theme_service_.get());
  LeaveSystemThemeSoon();
}

KoanSystemThemeLock::~KoanSystemThemeLock() = default;

void KoanSystemThemeLock::OnThemeChanged() {
  LeaveSystemThemeSoon();
}

void KoanSystemThemeLock::LeaveSystemThemeSoon() {
  if (!theme_service_->UsingSystemTheme()) {
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KoanSystemThemeLock::LeaveSystemTheme,
                                weak_factory_.GetWeakPtr()));
}

void KoanSystemThemeLock::LeaveSystemTheme() {
  // Another window's lock may have got there first.
  if (theme_service_->UsingSystemTheme()) {
    theme_service_->UseDefaultTheme();
  }
}

}  // namespace koan
