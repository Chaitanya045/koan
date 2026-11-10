// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_redact.h"

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

#include "base/strings/string_util.h"

namespace koan::agent {

namespace {

// The deepest a Node tree is followed; deeper nodes are emptied of their
// attributes rather than left unredacted.
constexpr int kMaxNodeDepth = 512;

constexpr auto kRawTextElements = std::to_array<std::string_view>(
    {"script", "style", "textarea", "title", "xmp", "iframe", "noembed",
     "noframes", "noscript", "plaintext"});

bool IsSecretType(std::string_view type) {
  const std::string_view trimmed =
      base::TrimString(type, base::kWhitespaceASCII, base::TRIM_ALL);
  return base::EqualsCaseInsensitiveASCII(trimmed, "hidden") ||
         base::EqualsCaseInsensitiveASCII(trimmed, "password");
}

bool IsHtmlSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

bool IsTagNameChar(char c) {
  return base::IsAsciiAlphaNumeric(c) || c == '-' || c == ':' || c == '_';
}

struct Attribute {
  std::string_view name;
  // The value's place in the tag, if it has one.
  size_t value_begin = 0;
  size_t value_end = 0;
  bool has_value = false;
};

// Finds `needle` from `from`, ignoring ASCII case.
size_t FindCaseInsensitive(std::string_view haystack,
                           std::string_view needle,
                           size_t from) {
  for (size_t i = from; i + needle.size() <= haystack.size(); ++i) {
    if (base::EqualsCaseInsensitiveASCII(haystack.substr(i, needle.size()),
                                         needle)) {
      return i;
    }
  }
  return std::string_view::npos;
}

void RedactNodes(base::DictValue& dict, int depth);

void RedactNodesIn(base::Value& value, int depth) {
  if (value.is_dict()) {
    RedactNodes(value.GetDict(), depth);
  } else if (value.is_list()) {
    for (base::Value& item : value.GetList()) {
      RedactNodesIn(item, depth + 1);
    }
  }
}

void RedactNodes(base::DictValue& dict, int depth) {
  const std::string* name = dict.FindString("nodeName");
  base::ListValue* attributes = dict.FindList("attributes");
  if (depth > kMaxNodeDepth) {
    if (attributes) {
      attributes->clear();
    }
    return;
  }
  if (name && attributes && base::EqualsCaseInsensitiveASCII(*name, "input")) {
    RedactSecretInputsInAttributes(*attributes);
  }
  for (auto [key, value] : dict) {
    RedactNodesIn(value, depth + 1);
  }
}

}  // namespace

std::string RedactSecretInputsInHtml(std::string_view html) {
  const size_t n = html.size();
  std::string out;
  out.reserve(n);
  size_t i = 0;
  while (i < n) {
    const size_t lt = html.find('<', i);
    if (lt == std::string_view::npos) {
      out.append(html.substr(i));
      break;
    }
    out.append(html.substr(i, lt - i));
    i = lt;
    if (html.substr(i).starts_with("<!--")) {
      size_t end = html.find("-->", i + 4);
      end = end == std::string_view::npos ? n : end + 3;
      out.append(html.substr(i, end - i));
      i = end;
      continue;
    }
    size_t j = i + 1;
    const bool closing = j < n && html[j] == '/';
    if (closing) {
      ++j;
    }
    const size_t name_begin = j;
    while (j < n && IsTagNameChar(html[j])) {
      ++j;
    }
    if (j == name_begin) {
      out.push_back('<');
      ++i;
      continue;
    }
    const std::string tag =
        base::ToLowerASCII(html.substr(name_begin, j - name_begin));

    // The attributes, up to the '>' that ends the tag (quotes respected).
    std::vector<Attribute> attributes;
    bool ended = false;
    size_t k = j;
    while (k < n) {
      while (k < n && (IsHtmlSpace(html[k]) || html[k] == '/')) {
        ++k;
      }
      if (k >= n) {
        break;
      }
      if (html[k] == '>') {
        ended = true;
        ++k;
        break;
      }
      Attribute attribute;
      const size_t attr_begin = k;
      // An '=' as the first character belongs to the name (as in HTML).
      ++k;
      while (k < n && !IsHtmlSpace(html[k]) && html[k] != '/' &&
             html[k] != '>' && html[k] != '=') {
        ++k;
      }
      attribute.name = html.substr(attr_begin, k - attr_begin);
      size_t after_name = k;
      while (after_name < n && IsHtmlSpace(html[after_name])) {
        ++after_name;
      }
      if (after_name < n && html[after_name] == '=') {
        k = after_name + 1;
        while (k < n && IsHtmlSpace(html[k])) {
          ++k;
        }
        if (k < n && (html[k] == '"' || html[k] == '\'')) {
          const char quote = html[k];
          attribute.value_begin = k + 1;
          const size_t close = html.find(quote, k + 1);
          attribute.value_end = close == std::string_view::npos ? n : close;
          k = close == std::string_view::npos ? n : close + 1;
        } else {
          attribute.value_begin = k;
          while (k < n && !IsHtmlSpace(html[k]) && html[k] != '>') {
            ++k;
          }
          attribute.value_end = k;
        }
        attribute.has_value = true;
      }
      attributes.push_back(attribute);
    }
    const size_t tag_end = k;

    bool secret = false;
    if (!closing && tag == "input") {
      // A tag the string cuts off is not known not to be secret.
      secret = !ended;
      for (const Attribute& attribute : attributes) {
        if (base::EqualsCaseInsensitiveASCII(attribute.name, "type")) {
          secret =
              secret || (attribute.has_value &&
                         IsSecretType(html.substr(
                             attribute.value_begin,
                             attribute.value_end - attribute.value_begin)));
          break;  // the first one counts, as in HTML
        }
      }
    }
    if (!secret) {
      out.append(html.substr(i, tag_end - i));
    } else {
      size_t copied = i;
      for (const Attribute& attribute : attributes) {
        if (attribute.has_value &&
            base::EqualsCaseInsensitiveASCII(attribute.name, "value")) {
          out.append(html.substr(copied, attribute.value_begin - copied));
          copied = attribute.value_end;
        }
      }
      out.append(html.substr(copied, tag_end - copied));
    }
    i = tag_end;

    // The text of a raw text element is not markup.
    if (!closing && ended &&
        std::ranges::find(kRawTextElements, tag) != kRawTextElements.end()) {
      size_t end = FindCaseInsensitive(html, "</" + tag, i);
      end = end == std::string_view::npos ? n : end;
      out.append(html.substr(i, end - i));
      i = end;
    }
  }
  return out;
}

void RedactSecretInputsInAttributes(base::ListValue& attributes) {
  std::optional<bool> secret;
  for (size_t i = 0; i + 1 < attributes.size(); i += 2) {
    const std::string* name = attributes[i].GetIfString();
    const std::string* value = attributes[i + 1].GetIfString();
    if (name && value && base::EqualsCaseInsensitiveASCII(*name, "type")) {
      secret = IsSecretType(*value);
      break;
    }
  }
  if (!secret.value_or(false)) {
    return;
  }
  for (size_t i = 0; i + 1 < attributes.size(); i += 2) {
    const std::string* name = attributes[i].GetIfString();
    if (name && base::EqualsCaseInsensitiveASCII(*name, "value")) {
      attributes[i + 1] = base::Value("");
    }
  }
}

void RedactSecretInputs(std::string_view method, base::DictValue& params) {
  if (!method.starts_with("DOM.")) {
    return;
  }
  if (method == "DOM.getOuterHTML") {
    if (const std::string* html = params.FindString("outerHTML")) {
      params.Set("outerHTML", RedactSecretInputsInHtml(*html));
    }
    return;
  }
  if (method == "DOM.getAttributes") {
    if (base::ListValue* attributes = params.FindList("attributes")) {
      RedactSecretInputsInAttributes(*attributes);
    }
    return;
  }
  if (method == "DOM.attributeModified") {
    const std::string* name = params.FindString("name");
    if (name && base::EqualsCaseInsensitiveASCII(*name, "value")) {
      params.Set("value", "");
    }
    return;
  }
  RedactNodes(params, 0);
}

}  // namespace koan::agent
