// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_CLI_KOAN_CLI_UPLOAD_H_
#define KOAN_CLI_KOAN_CLI_UPLOAD_H_

#include <stddef.h>

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/time/time.h"
#include "base/values.h"

// `koan upload` and act's `upload` step (.agent/notes/agent-control.md
// section 9.4, "Uploads"): the files an agent gives a page's file chooser,
// checked here before anything is sent, Koan.upload's params, and what is
// printed of its answer. The paths are on the Mac that runs Koan (an agent
// on another machine copies the file there first, with scp). Koan checks
// them again, and the chooser's own rules, before the page gets them.
namespace koan::cli {

// As many files as one Koan.upload takes.
inline constexpr size_t kMaxUploadPaths = 32;

// How long Koan.upload waits for the page to open its file chooser (after
// act's click, or a click before `koan upload`).
inline constexpr base::TimeDelta kUploadWait = base::Seconds(5);

struct UploadPath {
  // Absolute, with links resolved.
  std::string path;
  // The name the page sees: the given path's last part.
  std::string name;
};

// Resolves each of `given` (relative ones against `cwd`) and checks that it
// exists, is a regular file and can be read. False with `error` (a Koan.*
// line that names the path) for the first that fails. Reads the files'
// metadata only.
bool ResolveUploadPaths(const std::vector<std::string>& given,
                        const base::FilePath& cwd,
                        std::vector<UploadPath>* paths,
                        std::string* error);

// Koan.upload's params.
base::DictValue UploadParams(const std::vector<UploadPath>& paths,
                             base::TimeDelta wait);

// Its answer: "ok: attached resume.pdf (120.4 KB)".
std::string UploadedLine(const base::DictValue& result);

}  // namespace koan::cli

#endif  // KOAN_CLI_KOAN_CLI_UPLOAD_H_
