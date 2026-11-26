// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_REMOTE_KOAN_SSH_TEXT_H_
#define KOAN_BROWSER_REMOTE_KOAN_SSH_TEXT_H_

#include <string>
#include <string_view>

namespace koan::remote {

// Text from ssh that can quote the remote server (a keyboard-interactive
// prompt, a disconnect reason, a banner), made safe to show: at most
// `max_bytes` of valid UTF-8, invalid sequences replaced by U+FFFD, control
// characters (C0 but '\n' when `keep_newlines`, DEL, C1) and bidi controls
// removed, tabs turned into spaces. It's still the server's words: the UI
// shows it as quoted, untrusted text.
std::string SanitizeSshText(std::string_view text,
                            size_t max_bytes,
                            bool keep_newlines);

}  // namespace koan::remote

#endif  // KOAN_BROWSER_REMOTE_KOAN_SSH_TEXT_H_
