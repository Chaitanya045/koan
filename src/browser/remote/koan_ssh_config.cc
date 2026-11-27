// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_config.h"

#include <algorithm>
#include <iterator>
#include <set>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/functional/function_ref.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/threading/scoped_blocking_call.h"
#include "koan/browser/remote/koan_ssh_command.h"

namespace koan::remote {

namespace {

// readconf.c's READCONF_MAX_DEPTH.
constexpr int kMaxIncludeDepth = 16;
// Config files bigger than this aren't a user's ssh config.
constexpr size_t kMaxConfigSize = 1024 * 1024;
// Enough for anyone's picker.
constexpr size_t kMaxHosts = 1000;

bool IsBlank(char c) {
  return c == ' ' || c == '\t';
}

// misc.c argv_split(s, ..., terminate_on_comment=1): tokens split on blanks,
// '\'' and '"' quote (also mid-token), a backslash escapes a quote, a
// backslash or (outside quotes) a blank, and '#' at the start of a token ends
// the line. An unterminated quote makes the line invalid (empty result).
std::vector<std::string> SplitArgs(std::string_view s) {
  std::vector<std::string> args;
  size_t i = 0;
  while (i < s.size()) {
    if (IsBlank(s[i])) {
      ++i;
      continue;
    }
    if (s[i] == '#') {
      break;
    }
    std::string arg;
    char quote = 0;
    for (; i < s.size(); ++i) {
      const char c = s[i];
      if (c == '\\' && i + 1 < s.size()) {
        const char next = s[i + 1];
        if (next == '\'' || next == '"' || next == '\\' ||
            (quote == 0 && next == ' ')) {
          ++i;
          arg.push_back(next);
        } else {
          arg.push_back(c);
        }
      } else if (quote == 0 && IsBlank(c)) {
        break;
      } else if (quote == 0 && (c == '"' || c == '\'')) {
        quote = c;
      } else if (quote != 0 && c == quote) {
        quote = 0;
      } else {
        arg.push_back(c);
      }
    }
    if (quote != 0) {
      return {};
    }
    args.push_back(std::move(arg));
  }
  return args;
}

// readconf.c strdelim(): the keyword ends at a blank or '=' (one '=' may
// follow blanks), and the arguments start after it.
bool SplitKeyword(std::string_view line,
                  std::string* keyword,
                  std::string_view* rest) {
  size_t i = 0;
  while (i < line.size() && IsBlank(line[i])) {
    ++i;
  }
  const size_t start = i;
  while (i < line.size() && !IsBlank(line[i]) && line[i] != '=') {
    ++i;
  }
  if (i == start || line[start] == '#') {
    return false;
  }
  *keyword = base::ToLowerASCII(line.substr(start, i - start));
  while (i < line.size() && IsBlank(line[i])) {
    ++i;
  }
  if (i < line.size() && line[i] == '=') {
    ++i;
  }
  *rest = line.substr(i);
  return true;
}

bool HasWildcard(std::string_view text) {
  return text.find_first_of("*?[") != std::string_view::npos;
}

// Calls `directive` with each Host or Include line's lower-case keyword and
// arguments, in file order.
void ForEachHostOrInclude(
    std::string_view text,
    base::FunctionRef<void(std::string_view keyword,
                           std::vector<std::string> args)> directive) {
  for (std::string_view line : base::SplitStringPiece(
           text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    std::string keyword;
    std::string_view rest;
    if (SplitKeyword(line, &keyword, &rest) &&
        (keyword == "host" || keyword == "include")) {
      directive(keyword, SplitArgs(rest));
    }
  }
}

void ReadFileHosts(const base::FilePath& path,
                   const base::FilePath& home,
                   int depth,
                   std::set<base::FilePath>& visited,
                   std::vector<std::string>& hosts,
                   std::set<std::string>& seen) {
  if (depth > kMaxIncludeDepth || hosts.size() >= kMaxHosts ||
      !visited.insert(path).second) {
    return;
  }
  std::string text;
  if (!base::ReadFileToStringWithMaxSize(path, &text, kMaxConfigSize)) {
    return;
  }
  // An Include's hosts come where the Include is.
  ForEachHostOrInclude(
      text, [&](std::string_view keyword, std::vector<std::string> args) {
        if (keyword == "host") {
          for (std::string& pattern : args) {
            if (IsConcreteSshHost(pattern) && hosts.size() < kMaxHosts &&
                seen.insert(pattern).second) {
              hosts.push_back(std::move(pattern));
            }
          }
          return;
        }
        for (const std::string& include : args) {
          const base::FilePath resolved = ResolveSshInclude(include, home);
          // Only the last component may be a pattern.
          if (resolved.empty() || HasWildcard(resolved.DirName().value())) {
            continue;
          }
          const std::string name = resolved.BaseName().value();
          if (!HasWildcard(name)) {
            ReadFileHosts(resolved, home, depth + 1, visited, hosts, seen);
            continue;
          }
          std::vector<base::FilePath> matches;
          base::FileEnumerator files(resolved.DirName(), /*recursive=*/false,
                                     base::FileEnumerator::FILES, name);
          for (base::FilePath match = files.Next(); !match.empty();
               match = files.Next()) {
            matches.push_back(match);
          }
          std::ranges::sort(matches);
          for (const base::FilePath& match : matches) {
            ReadFileHosts(match, home, depth + 1, visited, hosts, seen);
          }
        }
      });
}

}  // namespace

SshConfigFile ParseSshConfig(std::string_view text) {
  SshConfigFile file;
  ForEachHostOrInclude(
      text, [&](std::string_view keyword, std::vector<std::string> args) {
        std::ranges::move(
            args, std::back_inserter(keyword == "host" ? file.host_patterns
                                                       : file.includes));
      });
  return file;
}

bool IsConcreteSshHost(std::string_view pattern) {
  return !pattern.empty() && pattern.front() != '!' &&
         pattern.find_first_of("*?") == std::string_view::npos &&
         IsValidSshHost(pattern);
}

base::FilePath ResolveSshInclude(std::string_view include,
                                 const base::FilePath& home) {
  if (include.empty() || home.empty() ||
      include.find_first_of("%$") != std::string_view::npos) {
    return base::FilePath();
  }
  if (include.starts_with("~/")) {
    return home.Append(include.substr(2));
  }
  if (include.starts_with('~')) {
    return base::FilePath();  // ~otheruser
  }
  const base::FilePath path(include);
  if (path.IsAbsolute()) {
    return path;
  }
  return home.Append(".ssh").Append(path);
}

std::vector<std::string> ReadSshConfigHosts(const base::FilePath& home) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  std::vector<std::string> hosts;
  std::set<std::string> seen;
  std::set<base::FilePath> visited;
  ReadFileHosts(home.Append(".ssh").Append("config"), home, /*depth=*/0,
                visited, hosts, seen);
  return hosts;
}

}  // namespace koan::remote
