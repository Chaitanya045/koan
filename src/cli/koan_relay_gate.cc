// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_relay_gate.h"

#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "crypto/hash.h"

namespace koan::cli {

namespace {

constexpr char kEnvName[] = "AGENT_BROWSER_CDP=";

// Compares without stopping at the first difference.
bool SameSecret(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  }
  return diff == 0;
}

std::string SessionDigest(std::string_view session) {
  return base::ToLowerASCII(base::HexEncode(crypto::hash::Sha256(session)))
      .substr(0, 16);
}

}  // namespace

RelayGate::RelayGate(std::string secret, uint16_t port, base::TimeTicks started)
    : secret_(std::move(secret)), port_(port), started_(started) {}

RelayGate::~RelayGate() = default;

RelayGate::Decision RelayGate::OnWebSocketRequest(
    int connection_id,
    std::string_view path,
    std::string_view host,
    const std::optional<std::string>& origin,
    base::TimeTicks now) {
  // A page's request always carries an Origin; agent-browser's never does.
  if (origin) {
    return Decision::kOriginRefused;
  }
  if (host != base::StrCat({"127.0.0.1:", base::NumberToString(port_)})) {
    return Decision::kBadHost;
  }
  constexpr std::string_view kPrefix = "/cdp/";
  if (!path.starts_with(kPrefix) ||
      !SameSecret(path.substr(kPrefix.size()), secret_)) {
    return Decision::kNotFound;
  }
  if (!used_ && now - started_ > kRelayFirstConnection) {
    return Decision::kExpired;
  }
  if (active_) {
    return Decision::kBusy;
  }
  active_ = connection_id;
  used_ = true;
  return Decision::kAccept;
}

void RelayGate::OnClosed(int connection_id) {
  if (active_ == connection_id) {
    active_.reset();
  }
}

bool RelayGate::FirstConnectionMissed(base::TimeTicks now) const {
  return !used_ && now - started_ > kRelayFirstConnection;
}

std::string RelayGate::Url() const {
  return base::StrCat(
      {"ws://127.0.0.1:", base::NumberToString(port_), "/cdp/", secret_});
}

std::string_view DecisionName(RelayGate::Decision decision) {
  switch (decision) {
    case RelayGate::Decision::kAccept:
      return "accepted";
    case RelayGate::Decision::kNotFound:
      return "not-found";
    case RelayGate::Decision::kOriginRefused:
      return "origin-refused";
    case RelayGate::Decision::kBadHost:
      return "bad-host";
    case RelayGate::Decision::kBusy:
      return "busy";
    case RelayGate::Decision::kExpired:
      return "expired";
  }
  return "unknown";
}

bool IsValidSessionName(std::string_view name) {
  if (name.empty() || name.size() > 64) {
    return false;
  }
  for (char c : name) {
    if (!base::IsAsciiAlphaNumeric(c) && c != '.' && c != '_' && c != '-') {
      return false;
    }
  }
  return true;
}

std::optional<std::string> SessionFromArgs(const std::vector<std::string>& args,
                                           const char* env_session,
                                           std::string* error) {
  std::string session;
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--session" && i + 1 < args.size()) {
      session = args[i + 1];
    } else if (args[i].starts_with("--session=")) {
      session = args[i].substr(sizeof("--session=") - 1);
    }
  }
  if (session.empty() && env_session && *env_session) {
    session = env_session;
  }
  if (session.empty()) {
    session = "default";
  }
  if (!IsValidSessionName(session)) {
    *error =
        "the agent-browser session must be 1 to 64 letters, digits, "
        "'.', '_' or '-'";
    return std::nullopt;
  }
  return session;
}

base::FilePath RelayHome(const base::FilePath& koan_home) {
  return koan_home.Append("relay");
}

base::FilePath RelayControlSocket(const base::FilePath& relay_home,
                                  std::string_view session) {
  return relay_home.Append("run").Append(SessionDigest(session) + ".sock");
}

base::FilePath RelayEnvFile(const base::FilePath& relay_home,
                            std::string_view session) {
  return relay_home.Append("run").Append(SessionDigest(session) + ".env");
}

std::string RelayEnvContents(std::string_view url) {
  return base::StrCat({kEnvName, url, "\n"});
}

std::optional<std::string> ParseRelayEnv(std::string_view contents) {
  contents = base::TrimWhitespaceASCII(contents, base::TRIM_ALL);
  if (!contents.starts_with(kEnvName)) {
    return std::nullopt;
  }
  std::string_view url = contents.substr(sizeof(kEnvName) - 1);
  if (!url.starts_with("ws://127.0.0.1:") ||
      url.find_first_of(" \n\r\t") != std::string_view::npos) {
    return std::nullopt;
  }
  return std::string(url);
}

}  // namespace koan::cli
