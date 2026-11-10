// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_redact.h"

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {
namespace {

TEST(KoanAgentRedactTest, EmptiesTheValueOfHiddenAndPasswordInputs) {
  EXPECT_EQ(RedactSecretInputsInHtml(
                R"(<form><input type="hidden" name="csrf" value="tok3n">)"
                R"(<INPUT TYPE=" Password " VALUE='hunter2'>)"
                R"(<input type="text" value="visible"></form>)"),
            R"(<form><input type="hidden" name="csrf" value="">)"
            R"(<INPUT TYPE=" Password " VALUE=''>)"
            R"(<input type="text" value="visible"></form>)");
  // Unquoted (a "/" is part of an unquoted value, as in HTML), the value
  // before the type, and more than one value.
  EXPECT_EQ(RedactSecretInputsInHtml(
                "<input value=abc type=hidden value=def/><p>x</p>"),
            "<input value= type=hidden value=><p>x</p>");
}

TEST(KoanAgentRedactTest, OnlyTheFirstTypeCounts) {
  EXPECT_EQ(RedactSecretInputsInHtml(
                R"(<input type="text" type="hidden" value="a">)"),
            R"(<input type="text" type="hidden" value="a">)");
  EXPECT_EQ(RedactSecretInputsInHtml(
                R"(<input type="hidden" type="text" value="a">)"),
            R"(<input type="hidden" type="text" value="">)");
}

TEST(KoanAgentRedactTest, IsNotFooledByQuotesCommentsOrScript) {
  // A '>' or a fake tag inside an attribute value does not end the tag.
  EXPECT_EQ(RedactSecretInputsInHtml(
                R"(<div title="a > <input type=text"><input type="password" )"
                R"(value="s3cret"></div>)"),
            R"(<div title="a > <input type=text"><input type="password" )"
            R"(value=""></div>)");
  // Markup in comments and scripts is not a tag.
  const std::string script =
      R"(<script>var s = "<input type=hidden value=x>";</script>)";
  EXPECT_EQ(RedactSecretInputsInHtml(script), script);
  const std::string comment = "<!-- <input type=password value=x> -->";
  EXPECT_EQ(RedactSecretInputsInHtml(comment), comment);
}

TEST(KoanAgentRedactTest, ATagCutOffIsSecret) {
  EXPECT_EQ(RedactSecretInputsInHtml(R"(<input value="abc)"),
            "<input value=\"");
  EXPECT_EQ(RedactSecretInputsInHtml("a < b"), "a < b");
}

TEST(KoanAgentRedactTest, AttributeListsAndNodes) {
  base::DictValue attributes = *base::JSONReader::ReadDict(
      R"({"attributes": ["name", "csrf", "type", "HIDDEN", "value", "t"]})",
      base::JSON_PARSE_RFC);
  RedactSecretInputs("DOM.getAttributes", attributes);
  EXPECT_EQ(*base::WriteJson(attributes),
            R"({"attributes":["name","csrf","type","HIDDEN","value",""]})");

  base::DictValue document = *base::JSONReader::ReadDict(
      R"({"root": {"nodeName": "#document", "children": [
           {"nodeName": "INPUT", "attributes": ["type", "password",
                                                "value", "p"]},
           {"nodeName": "INPUT", "attributes": ["type", "email",
                                                "value", "e"]},
           {"nodeName": "DIV", "attributes": ["type", "hidden",
                                              "value", "d"]}]}})",
      base::JSON_PARSE_RFC);
  RedactSecretInputs("DOM.getDocument", document);
  const std::string json = *base::WriteJson(document);
  EXPECT_EQ(json.find("\"p\""), std::string::npos);
  EXPECT_NE(json.find("\"e\""), std::string::npos);
  EXPECT_NE(json.find("\"d\""), std::string::npos);

  base::DictValue modified;
  modified.Set("nodeId", 3);
  modified.Set("name", "value");
  modified.Set("value", "typed");
  RedactSecretInputs("DOM.attributeModified", modified);
  EXPECT_EQ(*modified.FindString("value"), "");

  base::DictValue html;
  html.Set("outerHTML", R"(<input type=hidden value=x>)");
  RedactSecretInputs("Page.getFrameTree", html);
  EXPECT_EQ(*html.FindString("outerHTML"), "<input type=hidden value=x>");
}

}  // namespace
}  // namespace koan::agent
