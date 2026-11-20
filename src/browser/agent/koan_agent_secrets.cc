// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_secrets.h"

#include <algorithm>
#include <array>
#include <deque>
#include <utility>

#include "base/base64.h"
#include "base/json/string_escape.h"
#include "base/strings/string_util.h"

namespace koan::agent {

namespace {

std::string JsonEscaped(std::string_view text) {
  std::string out;
  base::EscapeJSONString(text, /*put_in_quotes=*/false, &out);
  return out;
}

// As JavaScript's JSON.stringify writes a string, without the quotes: only
// quotes, backslashes and control characters are escaped, with lower-case hex.
std::string JsStringified(std::string_view text) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    const auto byte = static_cast<unsigned char>(c);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (byte < 0x20) {
          out += "\\u00";
          out.push_back(kHex[byte >> 4]);
          out.push_back(kHex[byte & 0xf]);
        } else {
          out.push_back(c);
        }
    }
  }
  return out;
}

// encodeURIComponent() (`form`: false) or application/x-www-form-urlencoded
// (`form`: true, a space is "+"), with upper- or lower-case hex.
std::string UrlEncoded(std::string_view text, bool form, bool upper) {
  const char* hex = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  std::string out;
  for (char c : text) {
    const auto byte = static_cast<unsigned char>(c);
    const bool kept =
        base::IsAsciiAlphaNumeric(c) || c == '-' || c == '_' || c == '.' ||
        c == '*' ||
        (!form && (c == '!' || c == '~' || c == '\'' || c == '(' || c == ')'));
    if (kept) {
      out.push_back(c);
    } else if (form && c == ' ') {
      out.push_back('+');
    } else {
      out.push_back('%');
      out.push_back(hex[byte >> 4]);
      out.push_back(hex[byte & 0xf]);
    }
  }
  return out;
}

// The part of the base64 encoding of `value` that is the same wherever
// `value` sits in a longer payload, for each of the three byte alignments,
// in the standard and the URL-safe alphabet.
void AddBase64Runs(std::string_view value, std::set<std::string>& needles) {
  for (size_t offset = 0; offset < 3; ++offset) {
    std::string padded(offset, '\0');
    padded.append(value);
    const std::string encoded = base::Base64Encode(padded);
    // The first group holds padding bytes unless the alignment is 0; the
    // last one, unless full, depends on what follows the value.
    const size_t begin = offset ? 4 : 0;
    const size_t end = 4 * (padded.size() / 3);
    if (end <= begin || end - begin < kMinBase64Needle) {
      continue;
    }
    std::string run = encoded.substr(begin, end - begin);
    std::string url_safe = run;
    std::replace(url_safe.begin(), url_safe.end(), '+', '-');
    std::replace(url_safe.begin(), url_safe.end(), '/', '_');
    needles.insert(std::move(run));
    needles.insert(std::move(url_safe));
  }
}

bool IsHex(char c) {
  return base::IsHexDigit(c);
}

// Whether the backslash at `at` starts an escape (it is not itself escaped).
bool IsUnescapedBackslash(std::string_view text, size_t at) {
  if (text[at] != '\\') {
    return false;
  }
  size_t run = 0;
  while (at > run && text[at - run - 1] == '\\') {
    ++run;
  }
  return run % 2 == 0;
}

// Where an occurrence that starts at `start` must start so that it does not
// cut a JSON escape: at the escape's backslash if `start` is inside one.
size_t EscapeStart(std::string_view text, size_t start) {
  if (start > 0 && IsUnescapedBackslash(text, start - 1)) {
    return start - 1;
  }
  // Inside "\uXXXX": after "\u" and up to three hex digits.
  for (size_t digits = 0; digits <= 3 && start >= digits + 2; ++digits) {
    const size_t backslash = start - digits - 2;
    if (text[backslash + 1] != 'u' || !IsUnescapedBackslash(text, backslash)) {
      continue;
    }
    bool hex = true;
    for (size_t i = backslash + 2; i < start; ++i) {
      hex = hex && IsHex(text[i]);
    }
    if (hex) {
      return backslash;
    }
  }
  return start;
}

}  // namespace

std::vector<std::string> SecretNeedles(std::string_view secret) {
  if (secret.size() < kMinSecretLength) {
    return {};
  }
  std::set<std::string> needles;
  // As the facade's JSON writer writes the value, and as it writes a string
  // the page made with JSON.stringify().
  const std::string stringified = JsStringified(secret);
  needles.insert(JsonEscaped(secret));
  needles.insert(JsonEscaped(stringified));
  // URL-encoded (a form body, a query string), which needs no JSON escape.
  std::set<std::string> encoded;
  for (bool form : {false, true}) {
    for (bool upper : {true, false}) {
      encoded.insert(UrlEncoded(secret, form, upper));
    }
  }
  needles.insert(encoded.begin(), encoded.end());
  // base64 of the value, of its URL-encoded forms and of its JSON form.
  AddBase64Runs(secret, needles);
  AddBase64Runs(stringified, needles);
  for (const std::string& form : encoded) {
    AddBase64Runs(form, needles);
  }
  std::erase_if(needles, [](const std::string& needle) {
    return needle.size() < kMinSecretLength;
  });
  return std::vector<std::string>(needles.begin(), needles.end());
}

SecretScrubber::SecretScrubber() {
  Build();
}

SecretScrubber::~SecretScrubber() = default;

bool SecretScrubber::Add(std::string_view secret) {
  if (secret.size() < kMinSecretLength || secrets_.contains(secret)) {
    return false;
  }
  secrets_.emplace(secret);
  Build();
  return true;
}

void SecretScrubber::Clear() {
  secrets_.clear();
  Build();
}

void SecretScrubber::Build() {
  nodes_.assign(1, Node());
  for (const std::string& secret : secrets_) {
    for (const std::string& needle : SecretNeedles(secret)) {
      int32_t state = 0;
      for (char c : needle) {
        const auto byte = static_cast<uint8_t>(c);
        auto& next = nodes_[state].next;
        auto it = std::ranges::lower_bound(next, byte, {},
                                           &std::pair<uint8_t, int32_t>::first);
        if (it != next.end() && it->first == byte) {
          state = it->second;
          continue;
        }
        const auto child = static_cast<int32_t>(nodes_.size());
        next.insert(it, {byte, child});
        nodes_.emplace_back();
        state = child;
      }
      nodes_[state].match = std::max<uint32_t>(
          nodes_[state].match, static_cast<uint32_t>(needle.size()));
    }
  }
  // Failure links, breadth first; a node's match also counts the needles
  // that end in its failure chain.
  std::deque<int32_t> queue;
  for (const auto& [byte, child] : nodes_[0].next) {
    nodes_[child].fail = 0;
    queue.push_back(child);
  }
  while (!queue.empty()) {
    const int32_t state = queue.front();
    queue.pop_front();
    for (const auto& [byte, child] : nodes_[state].next) {
      int32_t fail = nodes_[state].fail;
      while (true) {
        const auto& next = nodes_[fail].next;
        auto it = std::ranges::lower_bound(next, byte, {},
                                           &std::pair<uint8_t, int32_t>::first);
        if (it != next.end() && it->first == byte && it->second != child) {
          fail = it->second;
          break;
        }
        if (fail == 0) {
          break;
        }
        fail = nodes_[fail].fail;
      }
      nodes_[child].fail = fail;
      nodes_[child].match = std::max(nodes_[child].match, nodes_[fail].match);
      queue.push_back(child);
    }
  }
}

int32_t SecretScrubber::Step(int32_t state, uint8_t byte) const {
  while (true) {
    const auto& next = nodes_[state].next;
    auto it = std::ranges::lower_bound(next, byte, {},
                                       &std::pair<uint8_t, int32_t>::first);
    if (it != next.end() && it->first == byte) {
      return it->second;
    }
    if (state == 0) {
      return 0;
    }
    state = nodes_[state].fail;
  }
}

size_t SecretScrubber::Scrub(std::string& text) const {
  if (secrets_.empty() || text.size() < kMinSecretLength) {
    return 0;
  }
  // The ranges to replace, in order, merged where they overlap.
  std::vector<std::pair<size_t, size_t>> ranges;
  int32_t state = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    state = Step(state, static_cast<uint8_t>(text[i]));
    const uint32_t length = nodes_[state].match;
    if (!length) {
      continue;
    }
    const size_t end = i + 1;
    size_t start = EscapeStart(text, end - length);
    // A longer occurrence may cover several earlier ones.
    while (!ranges.empty() && start <= ranges.back().second) {
      start = std::min(start, ranges.back().first);
      ranges.pop_back();
    }
    ranges.emplace_back(start, end);
  }
  if (ranges.empty()) {
    return 0;
  }
  std::string out;
  out.reserve(text.size());
  size_t copied = 0;
  for (const auto& [start, end] : ranges) {
    out.append(text, copied, start - copied);
    out.append(kSecretMark);
    copied = end;
  }
  out.append(text, copied, std::string::npos);
  text = std::move(out);
  return ranges.size();
}

}  // namespace koan::agent
