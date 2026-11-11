// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_audit.h"

#include <algorithm>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "crypto/hash.h"
#include "crypto/hmac.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace koan::agent {

namespace {

constexpr char kExtension[] = ".jsonl";

std::string_view OutcomeName(AuditOutcome outcome) {
  switch (outcome) {
    case AuditOutcome::kOk:
      return "ok";
    case AuditOutcome::kDenied:
      return "denied";
    case AuditOutcome::kPaused:
      return "paused";
    case AuditOutcome::kError:
      return "error";
  }
  return "error";
}

std::string DayOf(base::Time time) {
  base::Time::Exploded exploded;
  time.UTCExplode(&exploded);
  return base::StringPrintf("%04d-%02d-%02d", exploded.year, exploded.month,
                            exploded.day_of_month);
}

// The start of the UTC day a file is named after, or nullopt for a file that
// is not one of ours.
std::optional<base::Time> DayStart(const base::FilePath& file) {
  const std::string name = file.BaseName().AsUTF8Unsafe();
  int year = 0, month = 0, day = 0;
  if (name.size() != 10 + sizeof(kExtension) - 1 ||
      !name.ends_with(kExtension) ||
      !base::StringToInt(std::string_view(name).substr(0, 4), &year) ||
      name[4] != '-' ||
      !base::StringToInt(std::string_view(name).substr(5, 2), &month) ||
      name[7] != '-' ||
      !base::StringToInt(std::string_view(name).substr(8, 2), &day)) {
    return std::nullopt;
  }
  base::Time::Exploded exploded = {};
  exploded.year = year;
  exploded.month = month;
  exploded.day_of_month = day;
  base::Time start;
  if (!base::Time::FromUTCExploded(exploded, &start)) {
    return std::nullopt;
  }
  return start;
}

std::array<uint8_t, 32> Mac(const AuditLog::ChainKey& key,
                            std::string_view body) {
  return crypto::hmac::SignSha256(key, base::as_byte_span(body));
}

}  // namespace

AuditRecord::AuditRecord() = default;
AuditRecord::AuditRecord(const AuditRecord&) = default;
AuditRecord& AuditRecord::operator=(const AuditRecord&) = default;
AuditRecord::AuditRecord(AuditRecord&&) = default;
AuditRecord& AuditRecord::operator=(AuditRecord&&) = default;
AuditRecord::~AuditRecord() = default;

std::string SummarizeUrl(const GURL& url, bool full_urls) {
  if (!url.is_valid()) {
    return "(invalid)";
  }
  if (full_urls) {
    return url.spec();
  }
  const url::Origin origin = url::Origin::Create(url);
  std::string rest = url.PathForRequest();
  if (url.has_ref()) {
    base::StrAppend(&rest, {"#", url.ref()});
  }
  const auto digest = crypto::hash::Sha256(rest);
  const std::string where =
      origin.opaque() ? base::StrCat({url.scheme(), ":"}) : origin.Serialize();
  return base::StrCat(
      {where, " path#", base::HexEncodeLower(base::span(digest).first<8>())});
}

std::string SummarizeText(std::string_view text) {
  return base::StrCat({"text length ", base::NumberToString(text.size())});
}

std::string SummarizeScript(std::string_view source) {
  return base::StrCat({"script sha256 ",
                       base::HexEncodeLower(crypto::hash::Sha256(source)),
                       " length ", base::NumberToString(source.size())});
}

AuditLog::AuditLog(base::FilePath dir, ChainKey key, Limits limits)
    : dir_(std::move(dir)), key_(key), limits_(limits) {}

AuditLog::~AuditLog() = default;

std::string AuditLog::LineBody(const AuditRecord& record) {
  base::DictValue dict;
  dict.Set("time", record.time.InMillisecondsFSinceUnixEpoch());
  dict.Set("event", record.event);
  if (!record.client_id.empty()) {
    dict.Set("client", record.client_id);
  }
  if (!record.client_label.empty()) {
    dict.Set("label", record.client_label);
  }
  if (record.connection_id) {
    dict.Set("connection", base::NumberToString(record.connection_id));
  }
  if (!record.peer_chain.empty()) {
    dict.Set("peer", record.peer_chain);
  }
  if (!record.tab_id.empty()) {
    dict.Set("tab", record.tab_id);
  }
  if (!record.tab_origin.empty()) {
    dict.Set("origin", record.tab_origin);
  }
  if (!record.summary.empty()) {
    dict.Set("summary", record.summary);
  }
  dict.Set("outcome", OutcomeName(record.outcome));
  if (!record.duration.is_zero()) {
    dict.Set("ms", base::saturated_cast<int>(record.duration.InMilliseconds()));
  }
  return base::WriteJson(dict).value_or("{}");
}

bool AuditLog::VerifyChain(const std::vector<std::string>& lines,
                           const ChainKey& key) {
  std::optional<std::string> expected_prev;
  for (const std::string& line : lines) {
    std::optional<base::DictValue> dict =
        base::JSONReader::ReadDict(line, base::JSON_PARSE_RFC);
    if (!dict) {
      return false;
    }
    std::optional<base::Value> mac = dict->Extract("mac");
    const std::string* prev = dict->FindString("prev");
    if (!mac || !mac->is_string() || !prev) {
      return false;
    }
    if (expected_prev && *prev != *expected_prev) {
      return false;
    }
    const std::optional<std::string> body = base::WriteJson(*dict);
    if (!body || base::HexEncodeLower(Mac(key, *body)) != mac->GetString()) {
      return false;
    }
    expected_prev = mac->GetString();
  }
  return true;
}

bool AuditLog::Append(const AuditRecord& record) {
  if (!EnsureDirectory()) {
    return false;
  }
  if (!started_) {
    started_ = true;
    Prune(record.time);
    AuditRecord start;
    start.time = record.time;
    start.event = "start";
    Append(start);
  }
  const std::string day = DayOf(record.time);
  if (day != day_) {
    day_ = day;
    day_truncated_ = false;
    Prune(record.time);
  }
  if (day_truncated_) {
    return false;
  }
  const base::FilePath file = dir_.AppendASCII(day + kExtension);
  const std::string body = LineBody(record);
  const int64_t size = base::GetFileSize(file).value_or(0);
  if (size + static_cast<int64_t>(body.size()) + 200 >
      limits_.max_bytes_per_day) {
    day_truncated_ = true;
    AuditRecord truncated;
    truncated.time = record.time;
    truncated.event = "log-truncated";
    truncated.summary = "the day's log is full; later records are dropped";
    WriteLine(file, LineBody(truncated));
    return false;
  }
  return WriteLine(file, body);
}

bool AuditLog::EnsureDirectory() {
  if (!base::CreateDirectory(dir_)) {
    return false;
  }
#if BUILDFLAG(IS_POSIX)
  return base::SetPosixFilePermissions(dir_, 0700);
#else
  return true;
#endif
}

void AuditLog::Prune(base::Time now) {
  struct LogFile {
    base::FilePath path;
    base::Time day;
    int64_t size;
  };
  std::vector<LogFile> files;
  base::FileEnumerator enumerator(dir_, /*recursive=*/false,
                                  base::FileEnumerator::FILES);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    if (std::optional<base::Time> day = DayStart(path)) {
      files.push_back({path, *day, enumerator.GetInfo().GetSize()});
    }
  }
  std::ranges::sort(files, {}, &LogFile::day);
  const std::string today = DayOf(now);
  int64_t total = 0;
  for (const LogFile& file : files) {
    total += file.size;
  }
  for (const LogFile& file : files) {
    const bool too_old = file.day + limits_.max_age + base::Days(1) < now;
    const bool over_total = total > limits_.max_bytes_total;
    if (!too_old && !over_total) {
      break;
    }
    if (DayOf(file.day) == today) {
      break;
    }
    if (base::DeleteFile(file.path)) {
      total -= file.size;
    }
  }
}

bool AuditLog::WriteLine(const base::FilePath& file, const std::string& body) {
  // The mac covers the previous mac (in the body as `prev`) and the record.
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
  if (!dict) {
    return false;
  }
  dict->Set("prev", base::HexEncodeLower(previous_mac_));
  const std::optional<std::string> chained = base::WriteJson(*dict);
  if (!chained) {
    return false;
  }
  const std::array<uint8_t, 32> mac = Mac(key_, *chained);
  dict->Set("mac", base::HexEncodeLower(mac));
  std::optional<std::string> line = base::WriteJson(*dict);
  if (!line) {
    return false;
  }
  line->push_back('\n');

  const bool existed = base::PathExists(file);
  base::File out(file, base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_APPEND);
  if (!out.IsValid()) {
    return false;
  }
#if BUILDFLAG(IS_POSIX)
  if (!existed && !base::SetPosixFilePermissions(file, 0600)) {
    return false;
  }
#endif
  if (!out.WriteAtCurrentPosAndCheck(base::as_byte_span(*line))) {
    return false;
  }
  previous_mac_ = mac;
  return true;
}

}  // namespace koan::agent
