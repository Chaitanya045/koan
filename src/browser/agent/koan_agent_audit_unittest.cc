// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_audit.h"

#include <string>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_split.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace koan::agent {

namespace {

constexpr AuditLog::ChainKey kKey = {1, 2, 3, 4, 5, 6, 7, 8};

base::Time Day(int day_of_month, int hour = 12) {
  base::Time::Exploded exploded = {};
  exploded.year = 2026;
  exploded.month = 11;
  exploded.day_of_month = day_of_month;
  exploded.hour = hour;
  base::Time time;
  CHECK(base::Time::FromUTCExploded(exploded, &time));
  return time;
}

AuditRecord Record(base::Time time, std::string summary = "s") {
  AuditRecord record;
  record.time = time;
  record.event = "Page.navigate";
  record.client_id = "c1";
  record.summary = std::move(summary);
  return record;
}

std::vector<std::string> Lines(const base::FilePath& file) {
  std::string contents;
  CHECK(base::ReadFileToString(file, &contents));
  return base::SplitString(contents, "\n", base::KEEP_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY);
}

}  // namespace

// T-sec-9: the log never holds a path, a query, typed text or a script.
TEST(KoanAgentAuditTest, SummariesCarryNoSecrets) {
  const std::string url =
      SummarizeUrl(GURL("https://bank.example/reset?token=s3cret#frag"),
                   /*full_urls=*/false);
  EXPECT_TRUE(url.starts_with("https://bank.example path#")) << url;
  EXPECT_EQ(url.find("s3cret"), std::string::npos);
  EXPECT_EQ(url.find("reset"), std::string::npos);
  EXPECT_EQ(url.size(), std::string("https://bank.example path#").size() + 16);
  // Another path, another hash.
  EXPECT_NE(url, SummarizeUrl(GURL("https://bank.example/other"), false));
  EXPECT_EQ(SummarizeUrl(GURL("https://a.example/x?y"), true),
            "https://a.example/x?y");
  EXPECT_EQ(SummarizeUrl(GURL("nope"), false), "(invalid)");
  EXPECT_TRUE(SummarizeUrl(GURL("data:text/html,secret"), false)
                  .starts_with("data: path#"));

  EXPECT_EQ(SummarizeText("hunter2"), "text length 7");
  const std::string script = SummarizeScript("document.cookie");
  EXPECT_EQ(script.find("cookie"), std::string::npos);
  EXPECT_TRUE(script.ends_with(" length 15")) << script;
}

TEST(KoanAgentAuditTest, WritesAChainedDayFile) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const base::FilePath dir = temp.GetPath().AppendASCII("audit");
  AuditLog log(dir, kKey, {});
  ASSERT_TRUE(log.Append(Record(Day(10))));
  ASSERT_TRUE(log.Append(Record(Day(10, 13))));

  const base::FilePath file = dir.AppendASCII("2026-11-10.jsonl");
  const std::vector<std::string> lines = Lines(file);
  ASSERT_EQ(lines.size(), 3u);  // start + two records
  EXPECT_NE(lines[0].find("\"event\":\"start\""), std::string::npos);
  EXPECT_TRUE(AuditLog::VerifyChain(lines, kKey));

  // An edit, a deletion in the middle and another key all show.
  std::vector<std::string> edited = lines;
  edited[1].replace(edited[1].find("\"s\""), 3, "\"t\"");
  EXPECT_FALSE(AuditLog::VerifyChain(edited, kKey));
  EXPECT_FALSE(AuditLog::VerifyChain({lines[0], lines[2]}, kKey));
  AuditLog::ChainKey other = kKey;
  other[0] = 9;
  EXPECT_FALSE(AuditLog::VerifyChain(lines, other));

#if BUILDFLAG(IS_POSIX)
  int mode = 0;
  ASSERT_TRUE(base::GetPosixFilePermissions(file, &mode));
  EXPECT_EQ(mode, 0600);
  ASSERT_TRUE(base::GetPosixFilePermissions(dir, &mode));
  EXPECT_EQ(mode, 0700);
#endif
}

TEST(KoanAgentAuditTest, AFullDayIsCutOffOnce) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  AuditLog::Limits limits;
  limits.max_bytes_per_day = 2000;
  AuditLog log(temp.GetPath(), kKey, limits);
  int written = 0;
  for (int i = 0; i < 50; ++i) {
    written += log.Append(Record(Day(10)));
  }
  EXPECT_GT(written, 0);
  EXPECT_LT(written, 50);
  const std::vector<std::string> lines =
      Lines(temp.GetPath().AppendASCII("2026-11-10.jsonl"));
  EXPECT_NE(lines.back().find("log-truncated"), std::string::npos);
  EXPECT_EQ(lines.size(), static_cast<size_t>(written) + 2);
  EXPECT_TRUE(AuditLog::VerifyChain(lines, kKey));
  // The next day starts again.
  EXPECT_TRUE(log.Append(Record(Day(11))));
}

TEST(KoanAgentAuditTest, OldAndExcessFilesGoFirst) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const base::FilePath dir = temp.GetPath();
  const std::string filler(600, 'x');
  for (const char* name : {"2026-09-01.jsonl", "2026-11-07.jsonl",
                           "2026-11-08.jsonl", "2026-11-09.jsonl"}) {
    ASSERT_TRUE(base::WriteFile(dir.AppendASCII(name), filler));
  }
  ASSERT_TRUE(base::WriteFile(dir.AppendASCII("notes.txt"), filler));
  AuditLog::Limits limits;
  limits.max_bytes_total = 1500;
  AuditLog log(dir, kKey, limits);
  ASSERT_TRUE(log.Append(Record(Day(10))));

  // 09-01 is past 30 days; then the oldest until under the total.
  EXPECT_FALSE(base::PathExists(dir.AppendASCII("2026-09-01.jsonl")));
  EXPECT_FALSE(base::PathExists(dir.AppendASCII("2026-11-07.jsonl")));
  EXPECT_TRUE(base::PathExists(dir.AppendASCII("2026-11-08.jsonl")));
  EXPECT_TRUE(base::PathExists(dir.AppendASCII("2026-11-09.jsonl")));
  EXPECT_TRUE(base::PathExists(dir.AppendASCII("2026-11-10.jsonl")));
  // Not ours: left alone.
  EXPECT_TRUE(base::PathExists(dir.AppendASCII("notes.txt")));
}

}  // namespace koan::agent
