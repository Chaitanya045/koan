// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_upload.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/path_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "chrome/common/chrome_paths.h"
#include "components/enterprise/connectors/core/connectors_prefs.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/agent/koan_agent_page_surfaces.h"
#include "koan/browser/agent/koan_agent_platform.h"
#include "koan/browser/agent/koan_agent_upload_logic.h"
#include "third_party/blink/public/mojom/choosers/file_chooser.mojom.h"

#if BUILDFLAG(IS_POSIX)
#include <sys/stat.h>
#endif

namespace koan::agent {

namespace {

using Mode = blink::mojom::FileChooserParams::Mode;

ChooserMode ModeOf(Mode mode) {
  switch (mode) {
    case Mode::kOpen:
      return ChooserMode::kOpen;
    case Mode::kOpenMultiple:
      return ChooserMode::kOpenMultiple;
    case Mode::kUploadFolder:
    case Mode::kOpenDirectory:
      return ChooserMode::kFolder;
    case Mode::kSave:
      return ChooserMode::kSave;
  }
  return ChooserMode::kSave;
}

FacadeBackend::UploadResult Error(std::string code, std::string message) {
  FacadeBackend::UploadResult result;
  result.error_code = std::move(code);
  result.error_message = std::move(message);
  return result;
}

// The name the page sees for each file: the one asked for, or the path's.
std::vector<std::string> NamesOf(
    const std::vector<FacadeBackend::UploadFile>& files) {
  std::vector<std::string> names;
  for (const FacadeBackend::UploadFile& file : files) {
    names.push_back(file.name.empty()
                        ? base::FilePath::FromUTF8Unsafe(file.path)
                              .BaseName()
                              .AsUTF8Unsafe()
                        : file.name);
  }
  return names;
}

// The folders and files no upload is taken from: Koan's own data (the
// profile, its cookies and logins), the agents' keys, the user's keys and the
// usual credential stores of cloud and developer tools (a page that talks an
// agent into attaching ~/.aws/credentials gets nothing).
std::vector<base::FilePath> RefusedRoots(content::WebContents* contents) {
  std::vector<base::FilePath> roots;
  base::FilePath user_data;
  if (base::PathService::Get(chrome::DIR_USER_DATA, &user_data)) {
    roots.push_back(user_data);
  }
  roots.push_back(contents->GetBrowserContext()->GetPath());
  const base::FilePath home = base::GetHomeDir();
  if (!home.empty()) {
    for (const char* folder :
         {".koan", ".ssh", ".gnupg", "Library/Keychains", "Library/Cookies",
          ".aws", ".azure", ".config/gcloud", ".config/gh", ".docker", ".kube",
          ".git-credentials", ".netrc", ".npmrc", ".pypirc"}) {
      roots.push_back(home.Append(folder));
    }
  }
  return roots;
}

}  // namespace

struct AgentUploads::Pending {
  base::WeakPtr<content::WebContents> contents;
  FacadeBackend::UploadRequest request;
  base::RepeatingCallback<std::string()> may_answer;
  FacadeBackend::UploadCallback done;
  // While it waits for the page to open a chooser.
  base::OneShotTimer timer;
  bool waited = false;
  // Its files are being read.
  bool checking = false;
};

struct AgentUploads::Checked {
  struct File {
    base::FilePath path;
    std::string name;
    int64_t size = 0;
  };
  std::optional<UploadRefusal> refusal;
  std::vector<File> files;
};

AgentUploads::AgentUploads() = default;
AgentUploads::~AgentUploads() = default;

void AgentUploads::Upload(content::WebContents* contents,
                          FacadeBackend::UploadRequest request,
                          base::RepeatingCallback<std::string()> may_answer,
                          FacadeBackend::UploadCallback done) {
  const uint64_t id = next_id_++;
  auto pending = std::make_unique<Pending>();
  pending->contents = contents->GetWeakPtr();
  pending->request = std::move(request);
  pending->may_answer = std::move(may_answer);
  pending->done = std::move(done);
  pending_[id] = std::move(pending);
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&AgentUploads::Try, weak_factory_.GetWeakPtr(), id));
}

void AgentUploads::OnFileChooserHeld(content::WebContents* contents) {
  for (const auto& [id, pending] : pending_) {
    if (pending->contents.get() == contents && !pending->checking) {
      // Later: the chooser is being held now.
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&AgentUploads::Try, weak_factory_.GetWeakPtr(), id));
    }
  }
}

void AgentUploads::Try(uint64_t id) {
  auto it = pending_.find(id);
  if (it == pending_.end() || it->second->checking) {
    return;
  }
  Pending& pending = *it->second;
  content::WebContents* contents = pending.contents.get();
  if (!contents) {
    Finish(id, Error("Koan.targetClosed", "the tab closed"));
    return;
  }
  // An administrator's scan of uploads (the File Attached connector) runs
  // only on Chrome's own path for the user's pick (FileSelectHelper).
  PrefService* prefs =
      user_prefs::UserPrefs::Get(contents->GetBrowserContext());
  if (prefs &&
      prefs->IsManagedPreference(enterprise_connectors::kOnFileAttachedPref)) {
    Finish(id, Error("Koan.notPermitted",
                     "your organisation scans the files uploaded in this "
                     "browser, and only the user's own pick is scanned; ask "
                     "the user to choose the file (Open in Koan's "
                     "indicator)"));
    return;
  }
  const std::optional<AgentPageSurfaces::HeldFileChooser> chooser =
      AgentPageSurfaces::Get().FileChooserOf(contents);
  if (!chooser) {
    if (pending.request.wait.is_positive() && !pending.waited) {
      pending.waited = true;
      pending.timer.Start(FROM_HERE, pending.request.wait,
                          base::BindOnce(&AgentUploads::OnTimedOut,
                                         base::Unretained(this), id));
      return;
    }
    if (pending.timer.IsRunning()) {
      return;
    }
    Finish(id, Error("Koan.noFileChooser",
                     pending.waited
                         ? "the page opened no file chooser in time; click "
                           "its file input or upload button first (koan act "
                           "<tab> upload @ref <path> does both)"
                         : "the page has no file chooser open; click its "
                           "file input or upload button first (koan act "
                           "<tab> upload @ref <path> does both)"));
    return;
  }
  pending.timer.Stop();
  const std::vector<std::string> names = NamesOf(pending.request.files);
  if (std::optional<UploadRefusal> refusal =
          CheckChooser(ModeOf(chooser->mode), names.size())) {
    Finish(id, Error(refusal->code, refusal->message));
    return;
  }
  std::vector<std::string> accept;
  for (const std::u16string& type : chooser->accept_types) {
    accept.push_back(base::UTF16ToUTF8(type));
  }
  if (std::optional<UploadRefusal> refusal = CheckAccept(accept, names)) {
    Finish(id, Error(refusal->code, refusal->message));
    return;
  }
  pending.checking = true;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&AgentUploads::CheckFiles, pending.request.files, names,
                     RefusedRoots(contents)),
      base::BindOnce(&AgentUploads::OnFilesChecked, weak_factory_.GetWeakPtr(),
                     id, chooser->id));
}

void AgentUploads::OnTimedOut(uint64_t id) {
  Try(id);
}

// static
AgentUploads::Checked AgentUploads::CheckFiles(
    std::vector<FacadeBackend::UploadFile> files,
    std::vector<std::string> names,
    std::vector<base::FilePath> refused_roots) {
  // As the files are compared: with their links resolved.
  for (base::FilePath& root : refused_roots) {
    const base::FilePath real = base::MakeAbsoluteFilePath(root);
    if (!real.empty()) {
      root = real;
    }
  }
  auto Refused = [](std::string code, std::string message) {
    Checked refused;
    refused.refusal = UploadRefusal{std::move(code), std::move(message)};
    return refused;
  };
  Checked checked;
  for (size_t i = 0; i < files.size() && i < names.size(); ++i) {
    const std::string& shown = files[i].path;
    const base::FilePath real =
        base::MakeAbsoluteFilePath(base::FilePath::FromUTF8Unsafe(shown));
    const std::string missing =
        RunsInFlatpak()
            ? "no file at " + shown +
                  " that Koan can read; the path is on the computer that "
                  "runs Koan, and Koan's Flatpak reads only the Documents, "
                  "Downloads, Music, Pictures and Videos folders (copy the "
                  "file into Downloads first)"
            : "no file at " + shown + " on " + kThisMachine +
                  "; the path is on " + kTheMachineThatRunsKoan +
                  " (an agent elsewhere copies the file there first, with "
                  "scp)";
    if (real.empty()) {
      return Refused("Koan.fileNotFound", missing);
    }
    int64_t size = 0;
#if BUILDFLAG(IS_POSIX)
    base::stat_wrapper_t info;
    if (base::File::Stat(real, &info) != 0) {
      return Refused("Koan.fileNotFound", missing);
    }
    if (S_ISDIR(info.st_mode)) {
      return Refused("Koan.notAFile",
                     shown +
                         " is a folder; give the files in it (folder "
                         "uploads are not supported yet)");
    }
    if (!S_ISREG(info.st_mode)) {
      return Refused("Koan.notAFile", shown + " is not a regular file");
    }
    size = info.st_size;
#else
    base::File::Info info;
    if (!base::GetFileInfo(real, &info)) {
      return Refused("Koan.fileNotFound", missing);
    }
    if (info.is_directory) {
      return Refused("Koan.notAFile",
                     shown +
                         " is a folder; give the files in it (folder "
                         "uploads are not supported yet)");
    }
    size = info.size;
#endif
    if (IsUnderAny(real, refused_roots)) {
      return Refused("Koan.notPermitted",
                     shown +
                         " is in a folder Koan never uploads from (Koan's "
                         "own data, the agents' keys in ~/.koan, ~/.ssh, "
                         "~/.gnupg, the keychains, cloud and developer "
                         "credentials such as ~/.aws or ~/.netrc)");
    }
    if (!base::PathIsReadable(real)) {
      return Refused("Koan.fileUnreadable",
                     shown + " can't be read (check its permissions)");
    }
    checked.files.push_back({real, names[i], size});
  }
  return checked;
}

void AgentUploads::OnFilesChecked(uint64_t id,
                                  uint64_t chooser_id,
                                  Checked checked) {
  auto it = pending_.find(id);
  if (it == pending_.end()) {
    return;
  }
  Pending& pending = *it->second;
  if (checked.refusal) {
    Finish(id, Error(checked.refusal->code, checked.refusal->message));
    return;
  }
  content::WebContents* contents = pending.contents.get();
  if (!contents) {
    Finish(id, Error("Koan.targetClosed", "the tab closed"));
    return;
  }
  // The user may have taken the tab while the files were read.
  if (std::string refused = pending.may_answer.Run(); !refused.empty()) {
    Finish(id, Error(refused,
                     refused == "Koan.paused"
                         ? "the user is using this tab; nothing was attached. "
                           "Wait until they hand it back"
                         : "this tab is no longer the agent's to change; "
                           "nothing was attached"));
    return;
  }
  const std::optional<AgentPageSurfaces::HeldFileChooser> chooser =
      AgentPageSurfaces::Get().FileChooserOf(contents);
  constexpr char kGone[] =
      "the page's file chooser went away (the page moved on, or the user "
      "opened it) before the files were attached";
  if (!chooser || chooser->id != chooser_id) {
    Finish(id, Error("Koan.noFileChooser", kGone));
    return;
  }
  // As FileSelectHelper converts the user's pick: the file, and the name the
  // page sees as File.name.
  std::vector<blink::mojom::FileChooserFileInfoPtr> infos;
  std::vector<std::string> names;
  std::vector<int64_t> sizes;
  base::ListValue attached;
  for (const Checked::File& file : checked.files) {
    infos.push_back(blink::mojom::FileChooserFileInfo::NewNativeFile(
        blink::mojom::NativeFileInfo::New(file.path,
                                          base::UTF8ToUTF16(file.name),
                                          std::vector<std::u16string>())));
    names.push_back(file.name);
    sizes.push_back(file.size);
    attached.Append(base::DictValue()
                        .Set("name", file.name)
                        .Set("size", static_cast<double>(file.size)));
  }
  if (!AgentPageSurfaces::Get().AnswerFileChooser(chooser_id,
                                                  std::move(infos))) {
    Finish(id, Error("Koan.noFileChooser", kGone));
    return;
  }
  FacadeBackend::UploadResult result;
  result.summary = UploadSummary(names, sizes);
  result.result.Set("files", std::move(attached));
  result.result.Set("summary", result.summary);
  Finish(id, std::move(result));
}

void AgentUploads::Finish(uint64_t id, FacadeBackend::UploadResult result) {
  auto it = pending_.find(id);
  if (it == pending_.end()) {
    return;
  }
  std::unique_ptr<Pending> pending = std::move(it->second);
  pending_.erase(it);
  std::move(pending->done).Run(std::move(result));
}

}  // namespace koan::agent
