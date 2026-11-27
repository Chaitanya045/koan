// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_REMOTE_KOAN_SSH_CONFIG_H_
#define KOAN_BROWSER_REMOTE_KOAN_SSH_CONFIG_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"

// The hosts of the user's OpenSSH client config (~/.ssh/config and the files
// it Includes), for the remote Space picker. Koan only lists the aliases;
// ssh itself reads the config when it connects, so Koan never has to
// understand any other keyword.
namespace koan::remote {

// What one config file says, as OpenSSH's readconf.c tokenizes it: a keyword
// (case-insensitive, separated from its arguments by whitespace or one '='),
// arguments split by argv_split() (quotes, backslash escapes, '#' starting a
// comment at the start of a token).
struct SshConfigFile {
  // Every argument of every "Host" line, in order, wildcards included.
  std::vector<std::string> host_patterns;
  // Every argument of every "Include" line, unexpanded.
  std::vector<std::string> includes;
};

SshConfigFile ParseSshConfig(std::string_view text);

// Whether a "Host" argument names one concrete host the picker can offer: not
// a pattern ('*', '?'), not a negation ('!'), and a valid destination
// (IsValidSshHost(), koan_ssh_command.h).
bool IsConcreteSshHost(std::string_view pattern);

// Where an Include argument points, for the user config: "~/" is `home`, and
// a relative path is relative to ~/.ssh (readconf.c). Empty if Koan can't
// expand it (%tokens or ${ENV}, which only ssh can expand for a given host).
base::FilePath ResolveSshInclude(std::string_view include,
                                 const base::FilePath& home);

// The concrete Host aliases of `home`/.ssh/config and its Includes, in config
// order without duplicates. Includes are followed best effort: up to
// OpenSSH's depth of 16, with wildcards in the last path component only
// (sorted, like glob(3)). Blocking (reads files).
std::vector<std::string> ReadSshConfigHosts(const base::FilePath& home);

}  // namespace koan::remote

#endif  // KOAN_BROWSER_REMOTE_KOAN_SSH_CONFIG_H_
