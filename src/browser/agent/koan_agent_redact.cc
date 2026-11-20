// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_redact.h"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/strings/string_util.h"

namespace koan::agent {

namespace {

// The deepest a Node tree is followed; deeper nodes are emptied of their
// attributes rather than left unredacted.
constexpr int kMaxNodeDepth = 512;

constexpr auto kRawTextElements = std::to_array<std::string_view>(
    {"script", "style", "textarea", "title", "xmp", "iframe", "noembed",
     "noframes", "noscript", "plaintext"});

bool IsSecretType(std::string_view type, bool hidden = true) {
  const std::string_view trimmed =
      base::TrimString(type, base::kWhitespaceASCII, base::TRIM_ALL);
  return (hidden && base::EqualsCaseInsensitiveASCII(trimmed, "hidden")) ||
         base::EqualsCaseInsensitiveASCII(trimmed, "password");
}

// What RedactNodes() hides: hidden inputs' values too, and the nodes whose
// value is a secret whatever their type.
struct NodeRedaction {
  bool hidden = true;
  raw_ptr<const std::set<int>> fields = nullptr;
};

// Empties every `value` of a flat [name, value, ...] list if `secret`, or
// if its first `type` is a secret type.
void RedactAttributes(base::ListValue& attributes, bool hidden, bool secret) {
  if (!secret) {
    for (size_t i = 0; i + 1 < attributes.size(); i += 2) {
      const std::string* name = attributes[i].GetIfString();
      const std::string* value = attributes[i + 1].GetIfString();
      if (name && value && base::EqualsCaseInsensitiveASCII(*name, "type")) {
        secret = IsSecretType(*value, hidden);
        break;
      }
    }
  }
  if (!secret) {
    return;
  }
  for (size_t i = 0; i + 1 < attributes.size(); i += 2) {
    const std::string* name = attributes[i].GetIfString();
    if (name && base::EqualsCaseInsensitiveASCII(*name, "value")) {
      attributes[i + 1] = base::Value("");
    }
  }
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

void RedactNodes(base::DictValue& dict,
                 int depth,
                 const NodeRedaction& redaction);

void RedactNodesIn(base::Value& value,
                   int depth,
                   const NodeRedaction& redaction) {
  if (value.is_dict()) {
    RedactNodes(value.GetDict(), depth, redaction);
  } else if (value.is_list()) {
    for (base::Value& item : value.GetList()) {
      RedactNodesIn(item, depth + 1, redaction);
    }
  }
}

void RedactNodes(base::DictValue& dict,
                 int depth,
                 const NodeRedaction& redaction) {
  const std::string* name = dict.FindString("nodeName");
  base::ListValue* attributes = dict.FindList("attributes");
  if (depth > kMaxNodeDepth) {
    if (attributes) {
      attributes->clear();
    }
    return;
  }
  if (name && attributes) {
    const std::optional<int> node = dict.FindInt("backendNodeId");
    const bool secret_node =
        redaction.fields && node && redaction.fields->contains(*node);
    if (secret_node || base::EqualsCaseInsensitiveASCII(*name, "input")) {
      RedactAttributes(*attributes, redaction.hidden, secret_node);
    }
  }
  for (auto [key, value] : dict) {
    RedactNodesIn(value, depth + 1, redaction);
  }
}

// Accessibility nodes (CDP AXNode): a secret field's value is shown as
// filled or empty, and nothing inside it says more.
void SetAXText(base::DictValue& node, std::string_view key, std::string text) {
  base::DictValue value;
  value.Set("type", "string");
  value.Set("value", std::move(text));
  node.Set(key, std::move(value));
}

void RedactAXNodes(base::ListValue& nodes, const std::set<int>& fields) {
  if (fields.empty()) {
    return;
  }
  std::map<std::string, base::DictValue*, std::less<>> by_id;
  std::vector<base::DictValue*> secret;
  for (base::Value& item : nodes) {
    base::DictValue* node = item.GetIfDict();
    if (!node) {
      continue;
    }
    if (const std::string* id = node->FindString("nodeId")) {
      by_id[*id] = node;
    }
    const std::optional<int> backend = node->FindInt("backendDOMNodeId");
    if (backend && fields.contains(*backend)) {
      secret.push_back(node);
    }
  }
  std::set<const base::DictValue*> inner;
  std::vector<base::DictValue*> pending;
  for (base::DictValue* node : secret) {
    const std::string* value = node->FindStringByDottedPath("value.value");
    SetAXText(*node, "value",
              value && !value->empty() ? kSecretFilled : kSecretEmpty);
    pending.push_back(node);
  }
  while (!pending.empty()) {
    base::DictValue* node = pending.back();
    pending.pop_back();
    const base::ListValue* children = node->FindList("childIds");
    if (!children) {
      continue;
    }
    for (const base::Value& child : *children) {
      const std::string* id = child.GetIfString();
      auto it = id ? by_id.find(*id) : by_id.end();
      if (it == by_id.end() || !inner.insert(it->second).second) {
        continue;
      }
      if (it->second->FindDict("name")) {
        SetAXText(*it->second, "name", "");
      }
      if (it->second->FindDict("value")) {
        SetAXText(*it->second, "value", "");
      }
      pending.push_back(it->second);
    }
  }
}

}  // namespace

std::string RedactSecretInputsInHtml(std::string_view html) {
  return RedactInputsInHtml(html, /*hidden=*/true);
}

std::string RedactInputsInHtml(std::string_view html, bool hidden) {
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
                         IsSecretType(html.substr(attribute.value_begin,
                                                  attribute.value_end -
                                                      attribute.value_begin),
                                      hidden));
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
  RedactAttributes(attributes, /*hidden=*/true, /*secret=*/false);
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
  RedactNodes(params, 0, NodeRedaction());
}

void RedactSecretFields(std::string_view method,
                        base::DictValue& params,
                        const std::set<int>& fields,
                        bool hidden) {
  if (method.starts_with("Accessibility.")) {
    if (base::ListValue* nodes = params.FindList("nodes")) {
      RedactAXNodes(*nodes, fields);
    }
    for (std::string_view key : {"node", "root"}) {
      if (base::Value* node = params.Find(key); node && node->is_dict()) {
        base::ListValue one;
        one.Append(std::move(*node));
        RedactAXNodes(one, fields);
        params.Set(key, std::move(one.front()));
      }
    }
    return;
  }
  if (!method.starts_with("DOM.")) {
    return;
  }
  if (method == "DOM.getOuterHTML" || method == "DOM.attributeModified") {
    // Neither names the nodes by their backend id: by the input's type (on
    // a readable tab, every `value` change of an attribute too). A field's
    // `value` attribute is not what autofill filled, which the facade's
    // scrubber still catches if the page copies it there.
    if (hidden) {
      RedactSecretInputs(method, params);
    } else if (method == "DOM.getOuterHTML") {
      if (const std::string* html = params.FindString("outerHTML")) {
        params.Set("outerHTML", RedactInputsInHtml(*html, /*hidden=*/false));
      }
    }
    return;
  }
  if (method == "DOM.getAttributes") {
    if (base::ListValue* attributes = params.FindList("attributes")) {
      RedactAttributes(*attributes, hidden, /*secret=*/false);
    }
    return;
  }
  NodeRedaction redaction;
  redaction.hidden = hidden;
  redaction.fields = &fields;
  RedactNodes(params, 0, redaction);
}

}  // namespace koan::agent
