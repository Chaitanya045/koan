// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_UPLOAD_LOGIC_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_UPLOAD_LOGIC_H_

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"

// `koan upload` (.agent/notes/agent-control.md section 9.4, "Uploads"): the
// checks an agent's answer to a page's file chooser passes before Koan hands
// the files to the page, and what the indicator and the audit log say about
// it. Pure: koan_agent_upload.h reads the files and answers the chooser.
namespace koan::agent {

// The most files one answer takes. A page asks for one file, or a few; an
// agent that sends more is likely wrong about what it attaches.
inline constexpr size_t kMaxUploadFiles = 32;

// What the page's chooser asks for (blink::mojom::FileChooserParams::Mode).
enum class ChooserMode {
  // <input type=file>: one file.
  kOpen,
  // <input type=file multiple>.
  kOpenMultiple,
  // <input type=file webkitdirectory>, or a folder picker.
  kFolder,
  // A save dialog (content never lets a page ask for one).
  kSave,
};

struct UploadRefusal {
  // A Koan.* error code, and what the agent should do.
  std::string code;
  std::string message;
};

// Whether a chooser in `mode` takes `count` files: one for a single-file
// input, 1 to kMaxUploadFiles for `multiple`; never a folder (not supported
// yet) or a save dialog.
std::optional<UploadRefusal> CheckChooser(ChooserMode mode, size_t count);

// How a file named `file_name` stands against the chooser's `accept` list
// (lower-cased extensions such as ".pdf" and MIME types such as "image/*",
// as Blink passes them):
//  - kMatches: the list is empty, or an entry names the file's extension or
//    a MIME type its extension is known to have;
//  - kUnsure: no entry matches, but a MIME entry can't be checked (the
//    extension's type is not one Chrome knows without asking the OS);
//  - kMismatch: a clear mismatch (a person's open panel would grey it out).
enum class AcceptMatch { kMatches, kUnsure, kMismatch };
AcceptMatch MatchAccept(const std::vector<std::string>& accept_types,
                        std::string_view file_name);

// Koan.notAccepted for the first of `names` that is a clear mismatch
// (MatchAccept()), or nullopt. A file that might match goes: the page checks
// what it gets anyway.
std::optional<UploadRefusal> CheckAccept(
    const std::vector<std::string>& accept_types,
    const std::vector<std::string>& names);

// A name a page may see as File.name: not empty, at most 255 bytes of UTF-8,
// no path separator, not "." or "..".
bool IsPlainFileName(std::string_view name);

// Whether `path` is one of `roots` or inside one (the folders no upload is
// taken from: Koan's profile, its agent keys, ssh and keychain folders).
bool IsUnderAny(const base::FilePath& path,
                const std::vector<base::FilePath>& roots);

// The folders under `home` no upload is taken from: Koan's agent keys, and the
// credential stores of this platform (ssh and GnuPG keys, cloud and developer
// tokens; on the Mac the keychains and cookies; on Linux the keyrings,
// KWallet, the NSS database in ~/.pki, other browsers' profiles and every
// Flatpak app's data in ~/.var/app). Koan's own profile is refused by the
// caller, which knows where it is.
std::vector<base::FilePath> RefusedHomeFolders(const base::FilePath& home);

// "1.2 MB": a size for people (the audit log, the CLI's answer).
std::string SizeText(int64_t bytes);

// What the indicator says was attached: "resume.pdf", "resume.pdf and
// cover.pdf", "resume.pdf and 2 more files".
std::u16string AttachedText(const std::vector<std::string>& names);

// The audit log's summary of an upload: each file's name and size, never
// its contents: "resume.pdf (120.4 KB), cover.txt (12 B)".
std::string UploadSummary(const std::vector<std::string>& names,
                          const std::vector<int64_t>& sizes);

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_UPLOAD_LOGIC_H_
