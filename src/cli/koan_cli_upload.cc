// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_upload.h"

#include <sys/stat.h>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/strings/string_number_conversions.h"

namespace koan::cli {

bool ResolveUploadPaths(const std::vector<std::string>& given,
                        const base::FilePath& cwd,
                        std::vector<UploadPath>* paths,
                        std::string* error) {
  if (given.empty()) {
    *error = "usage: koan upload <tab> <path> [<path> ...]";
    return false;
  }
  if (given.size() > kMaxUploadPaths) {
    *error = "Koan.tooManyFiles: one upload takes at most " +
             base::NumberToString(kMaxUploadPaths) + " files";
    return false;
  }
  paths->clear();
  for (const std::string& text : given) {
    base::FilePath path(text);
    if (!path.IsAbsolute()) {
      path = cwd.Append(path);
    }
    const base::FilePath real = base::MakeAbsoluteFilePath(path);
    base::stat_wrapper_t info;
    if (text.empty() || real.empty() || base::File::Stat(real, &info) != 0) {
      *error = "Koan.fileNotFound: no file at " + text +
               " on this Mac (the paths are on the Mac that runs Koan; an "
               "agent elsewhere copies the file there first, with scp)";
      return false;
    }
    if (S_ISDIR(info.st_mode)) {
      *error = "Koan.notAFile: " + text +
               " is a folder; give the files in it (folder uploads are not "
               "supported yet)";
      return false;
    }
    if (!S_ISREG(info.st_mode)) {
      *error = "Koan.notAFile: " + text + " is not a regular file";
      return false;
    }
    if (!base::PathIsReadable(real)) {
      *error = "Koan.fileUnreadable: " + text +
               " can't be read (check its permissions)";
      return false;
    }
    paths->push_back({real.value(), path.BaseName().value()});
  }
  return true;
}

base::DictValue UploadParams(const std::vector<UploadPath>& paths,
                             base::TimeDelta wait) {
  base::ListValue files;
  for (const UploadPath& path : paths) {
    files.Append(
        base::DictValue().Set("path", path.path).Set("name", path.name));
  }
  base::DictValue params;
  params.Set("files", std::move(files));
  params.Set("waitMs", static_cast<int>(wait.InMilliseconds()));
  return params;
}

std::string UploadedLine(const base::DictValue& result) {
  const std::string* summary = result.FindString("summary");
  return "ok: attached " +
         (summary && !summary->empty() ? *summary : std::string("the files"));
}

}  // namespace koan::cli
