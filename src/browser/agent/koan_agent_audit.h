// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_AUDIT_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_AUDIT_H_

#include <stdint.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/time/time.h"
#include "base/values.h"

class GURL;

// Agent control's activity log (.agent/notes/agent-control.md section 6.3):
// one JSON object per line in <dir>/YYYY-MM-DD.jsonl (the UTC day), 0600 in a
// 0700 directory. Evidence for the user, not a security boundary: a process of
// the same user can delete the files.
//
// What is recorded never carries a secret (T20): a URL is its origin and a
// hash of the rest unless the user turned full URLs on; typed text is its
// length; script is its SHA-256 and length; headers, cookies and page content
// never. The helpers below make those summaries, and AuditLog writes only what
// they make plus the fixed fields.
//
// Integrity is best-effort: every line carries `mac`, an HMAC-SHA256 under a
// key the caller holds (Keychain-bound through os_crypt in the browser) of the
// previous line's mac and the line without its own, so an edit or a deletion in
// the middle of a launch's lines shows. Each launch starts a new chain (its
// first line is a `start` event with an all-zero previous mac).
//
// AuditLog does blocking file work: use it on a sequence that may block (the
// browser keeps it in a base::SequenceBound on a thread-pool sequence), never
// on the UI or IO thread.
namespace koan::agent {

// The record's outcome.
enum class AuditOutcome { kOk, kDenied, kPaused, kError };

struct AuditRecord {
  AuditRecord();
  AuditRecord(const AuditRecord&);
  AuditRecord& operator=(const AuditRecord&);
  AuditRecord(AuditRecord&&);
  AuditRecord& operator=(AuditRecord&&);
  ~AuditRecord();

  base::Time time;
  // A protocol method ("Page.navigate"), or for a lifecycle event its name
  // ("pair", "approve", "deny", "revoke", "take-over", "pause", "release",
  // "user-needed").
  std::string event;
  std::string client_id;
  std::string client_label;
  uint64_t connection_id = 0;
  // The peer: the verified executable and its agent ancestor, as the pairing
  // dialog names them.
  std::string peer_chain;
  std::string tab_id;
  // The tab's origin only.
  std::string tab_origin;
  // Made with the helpers below; never raw parameters.
  std::string summary;
  AuditOutcome outcome = AuditOutcome::kOk;
  base::TimeDelta duration;
};

// "https://example.com path#3f2a9c01d4e5b6a7" (the first 8 bytes of the
// SHA-256 of the path, query and fragment) or the full URL when the user turned
// full URLs on; "(invalid)" for a URL that does not parse. Magic links and
// reset tokens live in paths and queries, so they are not written by default.
std::string SummarizeUrl(const GURL& url, bool full_urls);
// "text length N": never the text.
std::string SummarizeText(std::string_view text);
// "script sha256 <hex> length N": never the source.
std::string SummarizeScript(std::string_view source);

class AuditLog {
 public:
  struct Limits {
    int64_t max_bytes_per_day = 20 * 1024 * 1024;
    int64_t max_bytes_total = 100 * 1024 * 1024;
    base::TimeDelta max_age = base::Days(30);
  };

  using ChainKey = std::array<uint8_t, 32>;

  AuditLog(base::FilePath dir, ChainKey key, Limits limits);
  AuditLog(const AuditLog&) = delete;
  AuditLog& operator=(const AuditLog&) = delete;
  ~AuditLog();

  // Appends the record. When the day's file is full, the first record that
  // does not fit is replaced by one `log-truncated` line and the rest of the
  // day is dropped; when all files are over the total, the oldest go first.
  // False if nothing was written.
  bool Append(const AuditRecord& record);

  // The JSON of a line without its mac: what the mac covers. Public for tests.
  static std::string LineBody(const AuditRecord& record);
  // Whether `lines` (one file, in order) form an unbroken chain from `start`
  // under `key`. For tests and a later "verify" button.
  static bool VerifyChain(const std::vector<std::string>& lines,
                          const ChainKey& key);

 private:
  bool EnsureDirectory();
  // Removes files past max_age, then the oldest until under max_bytes_total.
  void Prune(base::Time now);
  bool WriteLine(const base::FilePath& file, const std::string& body);

  const base::FilePath dir_;
  const ChainKey key_;
  const Limits limits_;
  std::array<uint8_t, 32> previous_mac_{};
  bool started_ = false;
  // The UTC day the last write went to, and whether it was cut off.
  std::string day_;
  bool day_truncated_ = false;
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_AUDIT_H_
