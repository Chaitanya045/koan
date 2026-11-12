// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/importer/koan_import_platform.h"

#import <AppKit/AppKit.h>

#include "base/apple/foundation_util.h"
#include "base/base64.h"
#include "base/strings/string_view_util.h"
#include "base/strings/sys_string_conversions.h"
#include "crypto/apple/keychain_v2.h"

namespace koan::importer {

std::optional<std::string> ReadKeychainSecret(std::string_view service,
                                              std::string_view account) {
  // The same lookup os_crypt makes for Koan's own item
  // (keychain_password_mac.mm); for another app's item macOS asks first.
  base::expected<std::vector<uint8_t>, OSStatus> secret =
      crypto::apple::KeychainV2::GetInstance().FindGenericPassword(service,
                                                                   account);
  if (!secret.has_value() || secret->empty()) {
    return std::nullopt;
  }
  return std::string(base::as_string_view(*secret));
}

std::string AppIconDataUrl(std::string_view bundle_id) {
  constexpr NSInteger kSize = 64;
  @autoreleasepool {
    NSURL* app = [NSWorkspace.sharedWorkspace
        URLForApplicationWithBundleIdentifier:base::SysUTF8ToNSString(
                                                  bundle_id)];
    if (!app) {
      return std::string();
    }
    NSImage* icon = [NSWorkspace.sharedWorkspace iconForFile:app.path];
    NSBitmapImageRep* bitmap =
        [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                pixelsWide:kSize
                                                pixelsHigh:kSize
                                             bitsPerSample:8
                                           samplesPerPixel:4
                                                  hasAlpha:YES
                                                  isPlanar:NO
                                            colorSpaceName:NSDeviceRGBColorSpace
                                               bytesPerRow:0
                                              bitsPerPixel:0];
    if (!icon || !bitmap) {
      return std::string();
    }
    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext.currentContext =
        [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
    [icon drawInRect:NSMakeRect(0, 0, kSize, kSize)
            fromRect:NSZeroRect
           operation:NSCompositingOperationCopy
            fraction:1.0];
    [NSGraphicsContext restoreGraphicsState];
    NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG
                                       properties:@{}];
    if (!png) {
      return std::string();
    }
    return "data:image/png;base64," +
           base::Base64Encode(base::apple::NSDataToSpan(png));
  }
}

}  // namespace koan::importer
