// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef KOAN_BROWSER_AGENT_KOAN_AGENT_SECRETS_H_
#define KOAN_BROWSER_AGENT_KOAN_AGENT_SECRETS_H_

#include <stddef.h>
#include <stdint.h>

#include <set>
#include <string>
#include <string_view>
#include <vector>

// The egress scrubber (.agent/notes/agent-control.md section 4.5, "Secrets
// Koan filled"): the backstop that keeps the secrets Koan filled for an agent
// (a password, a card number, an IBAN; `koan autofill`) out of everything the
// agent receives. The browser remembers each value it filled, in memory only,
// for its tab's life; the facade passes every message it sends to the agent
// through Scrub() as serialized JSON, so an answer, an event, a console
// message, a request body or a relayed frame that carries one gets
// "[secret]" in its place.
//
// A value is looked for as the JSON writer writes it, as JavaScript's
// JSON.stringify would inside a string the page made, URL-encoded (both
// encodeURIComponent and form encoding, either hex case) and base64 (standard
// and URL-safe, at each of the three byte alignments a value can have inside
// a longer payload). Values shorter than kMinSecretLength are not looked for
// (a CVC would match too much text); the facade's field lock and the
// snapshot redaction cover those. One pass over the message, whatever the
// number of values (Aho-Corasick). Pure logic.
namespace koan::agent {

inline constexpr size_t kMinSecretLength = 6;
// The shortest base64 run looked for: shorter runs of a value's encoding at
// an alignment would match ordinary text.
inline constexpr size_t kMinBase64Needle = 8;
// What a secret becomes.
inline constexpr char kSecretMark[] = "[secret]";

// Every form of `secret` (UTF-8) Scrub() looks for, without duplicates.
// Empty for a secret shorter than kMinSecretLength.
std::vector<std::string> SecretNeedles(std::string_view secret);

class SecretScrubber {
 public:
  SecretScrubber();
  SecretScrubber(const SecretScrubber&) = delete;
  SecretScrubber& operator=(const SecretScrubber&) = delete;
  ~SecretScrubber();

  // Remembers `secret` (UTF-8). False if it is too short to look for, or
  // already known.
  bool Add(std::string_view secret);
  void Clear();
  bool empty() const { return secrets_.empty(); }
  size_t size() const { return secrets_.size(); }

  // Replaces every occurrence of a form of a remembered secret in `text` (a
  // serialized JSON message) with kSecretMark, and returns how many it
  // replaced. Overlapping occurrences become one mark. An occurrence that
  // starts inside a JSON escape (after "\" or "\u00") takes the escape with
  // it, so that the message stays valid JSON.
  size_t Scrub(std::string& text) const;

 private:
  struct Node {
    // Sorted by byte.
    std::vector<std::pair<uint8_t, int32_t>> next;
    int32_t fail = 0;
    // The longest needle that ends here, counting the ones that end in a
    // suffix of this node's path (0: none).
    uint32_t match = 0;
  };
  void Build();
  int32_t Step(int32_t state, uint8_t byte) const;

  std::set<std::string, std::less<>> secrets_;
  std::vector<Node> nodes_;
};

}  // namespace koan::agent

#endif  // KOAN_BROWSER_AGENT_KOAN_AGENT_SECRETS_H_
