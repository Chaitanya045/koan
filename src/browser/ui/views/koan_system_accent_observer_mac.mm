// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/views/koan_system_accent_observer.h"

#import <AppKit/AppKit.h>

namespace koan {

void ObserveSystemAccentChanges(base::RepeatingClosure callback) {
  // The observer lives as long as the process.
  [NSNotificationCenter.defaultCenter
      addObserverForName:NSSystemColorsDidChangeNotification
                  object:nil
                   queue:NSOperationQueue.mainQueue
              usingBlock:^(NSNotification* note) {
                callback.Run();
              }];
}

}  // namespace koan
