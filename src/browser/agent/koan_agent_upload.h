// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_UPLOAD_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_UPLOAD_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "koan/browser/agent/koan_agent_facade.h"

namespace content {
class WebContents;
}  // namespace content

// `koan upload` in the browser (.agent/notes/agent-control.md section 9.4,
// "Uploads"; issue #420): an agent answers the file chooser a page in a tab
// it drives opened, with files it was given, as the user's pick in the open
// panel would. The chooser is the one AgentPageSurfaces holds as
// Koan.userNeeded {file-picker} (WebContentsImpl::RunFileChooser, through
// BrowserWebContentsDelegate::HoldFileChooser); the answer goes to its own
// content::FileSelectListener, as FileSelectHelper's does, so content grants
// the renderer read access to exactly those files and the page gets them
// with its input and change events. No open panel is shown: FileSelectHelper
// is not used (it would show the panel, and refuses a tab that isn't in
// front, which an agent's tab never is).
//
// Before it answers: the chooser's own rules (one file, or `multiple`; its
// `accept` list, refused on a clear mismatch; no folder, no save dialog);
// each path an absolute path to an existing, readable regular file (read on a
// blocking thread), not in Koan's profile, Koan's agent keys (~/.koan), ~/.ssh,
// ~/.gnupg, ~/Library/Keychains or the usual cloud and developer credential
// stores (~/.aws, ~/.netrc, ...); and not while an administrator's policy
// scans uploads (the File Attached enterprise connector, which only
// FileSelectHelper runs: the user picks then). No size limit: the page reads
// the file itself, nothing passes through the agent connection.
//
// One per AgentForwarder, on the UI thread.
namespace koan::agent {

class AgentUploads {
 public:
  AgentUploads();
  AgentUploads(const AgentUploads&) = delete;
  AgentUploads& operator=(const AgentUploads&) = delete;
  ~AgentUploads();

  // Answers the file chooser held for `contents` with `request`'s files once
  // one is (waiting up to `request.wait` for the page to open it). Just
  // before the answer, `may_answer` says whether the tab is still the
  // agent's to change: empty if it is, else the error code (Koan.paused: the
  // user has it; Koan.notPermitted: taken back, or only readable). `done`
  // runs once, never inside this call.
  void Upload(content::WebContents* contents,
              FacadeBackend::UploadRequest request,
              base::RepeatingCallback<std::string()> may_answer,
              FacadeBackend::UploadCallback done);

  // A file chooser was held for `contents`: an upload waiting for one goes on.
  void OnFileChooserHeld(content::WebContents* contents);

 private:
  struct Pending;
  struct Checked;

  // On a blocking thread: each of `files` (whose page names are `names`) is
  // an existing, readable regular file outside `refused_roots`.
  static Checked CheckFiles(std::vector<FacadeBackend::UploadFile> files,
                            std::vector<std::string> names,
                            std::vector<base::FilePath> refused_roots);

  // Answers `id` if its tab holds a chooser now; else it waits, or fails once
  // its wait is over.
  void Try(uint64_t id);
  void OnTimedOut(uint64_t id);
  // The files were read (on a blocking thread) for the held chooser
  // `chooser_id`.
  void OnFilesChecked(uint64_t id, uint64_t chooser_id, Checked checked);
  void Finish(uint64_t id, FacadeBackend::UploadResult result);

  std::map<uint64_t, std::unique_ptr<Pending>> pending_;
  uint64_t next_id_ = 1;
  base::WeakPtrFactory<AgentUploads> weak_factory_{this};
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_UPLOAD_H_
