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

TEST(KoanAgentRedactTest, SecretFieldsReadAsFilledOrEmptyInTheAXTree) {
  // A card number field (backend node 41) Koan filled, a CVC field (42) left
  // empty, and an email field (43).
  base::DictValue tree = *base::JSONReader::ReadDict(
      R"({"nodes": [
        {"nodeId": "1", "role": {"value": "RootWebArea"},
         "childIds": ["2", "4", "6"]},
        {"nodeId": "2", "role": {"value": "textbox"},
         "name": {"type": "computedString", "value": "Card number"},
         "value": {"type": "string", "value": "4242 4242 4242 4242"},
         "backendDOMNodeId": 41, "childIds": ["3"]},
        {"nodeId": "3", "role": {"value": "StaticText"},
         "name": {"type": "computedString", "value": "4242 4242 4242 4242"}},
        {"nodeId": "4", "role": {"value": "textbox"},
         "name": {"type": "computedString", "value": "CVC"},
         "value": {"type": "string", "value": ""}, "backendDOMNodeId": 42},
        {"nodeId": "6", "role": {"value": "textbox"},
         "name": {"type": "computedString", "value": "Email"},
         "value": {"type": "string", "value": "ada@example.com"},
         "backendDOMNodeId": 43}]})",
      base::JSON_PARSE_RFC);
  RedactSecretFields("Accessibility.getFullAXTree", tree, {41, 42},
                     /*hidden=*/false);
  const base::ListValue& nodes = *tree.FindList("nodes");
  EXPECT_EQ(*nodes[1].GetDict().FindStringByDottedPath("value.value"),
            kSecretFilled);
  // Its label stays: the agent sees which field it is.
  EXPECT_EQ(*nodes[1].GetDict().FindStringByDottedPath("name.value"),
            "Card number");
  EXPECT_EQ(*nodes[2].GetDict().FindStringByDottedPath("name.value"), "");
  EXPECT_EQ(*nodes[3].GetDict().FindStringByDottedPath("value.value"),
            kSecretEmpty);
  EXPECT_EQ(*nodes[4].GetDict().FindStringByDottedPath("value.value"),
            "ada@example.com");
  EXPECT_EQ(base::WriteJson(tree)->find("4242"), std::string::npos);

  // One node (getRootAXNode, loadComplete).
  base::DictValue root = *base::JSONReader::ReadDict(
      R"({"node": {"nodeId": "2", "backendDOMNodeId": 41,
                   "value": {"type": "string", "value": "x"}}})",
      base::JSON_PARSE_RFC);
  RedactSecretFields("Accessibility.getRootAXNode", root, {41}, false);
  EXPECT_EQ(*root.FindStringByDottedPath("node.value.value"), kSecretFilled);
}

TEST(KoanAgentRedactTest, ADrivenTabHidesPasswordsAndSecretFields) {
  base::DictValue document = *base::JSONReader::ReadDict(
      R"({"root": {"nodeName": "#document", "children": [
           {"nodeName": "INPUT", "backendNodeId": 5,
            "attributes": ["type", "password", "value", "p4ss"]},
           {"nodeName": "INPUT", "backendNodeId": 6,
            "attributes": ["type", "hidden", "value", "csrf"]},
           {"nodeName": "INPUT", "backendNodeId": 7,
            "attributes": ["type", "text", "value", "4111"]},
           {"nodeName": "INPUT", "backendNodeId": 8,
            "attributes": ["type", "text", "value", "Ada"]}]}})",
      base::JSON_PARSE_RFC);
  RedactSecretFields("DOM.getDocument", document, {7}, /*hidden=*/false);
  const std::string json = *base::WriteJson(document);
  EXPECT_EQ(json.find("p4ss"), std::string::npos);
  EXPECT_EQ(json.find("4111"), std::string::npos);
  // The agent's own tab keeps its hidden inputs and its other fields.
  EXPECT_NE(json.find("csrf"), std::string::npos);
  EXPECT_NE(json.find("Ada"), std::string::npos);

  base::DictValue html;
  html.Set("outerHTML",
           R"(<input type=password value=p4ss><input type=hidden value=t>)");
  RedactSecretFields("DOM.getOuterHTML", html, {}, false);
  EXPECT_EQ(*html.FindString("outerHTML"),
            "<input type=password value=><input type=hidden value=t>");
  // A readable tab hides hidden inputs too.
  RedactSecretFields("DOM.getOuterHTML", html, {}, /*hidden=*/true);
  EXPECT_EQ(*html.FindString("outerHTML"),
            "<input type=password value=><input type=hidden value=>");

  base::DictValue attributes = *base::JSONReader::ReadDict(
      R"({"attributes": ["type", "password", "value", "p4ss"]})",
      base::JSON_PARSE_RFC);
  RedactSecretFields("DOM.getAttributes", attributes, {}, false);
  EXPECT_EQ(base::WriteJson(attributes)->find("p4ss"), std::string::npos);
}

// S1 (#412 review): a card field is secret by its autocomplete too, in
// every DOM read, whatever its type and wherever the attribute is.
TEST(KoanAgentRedactTest, CardFieldsByTheirAutocomplete) {
  EXPECT_EQ(
      RedactInputsInHtml(
          R"(<input value="4111" AUTOCOMPLETE="section-pay CC-Number">)"
          R"(<input autocomplete="name" value="Ada">)"
          R"(<input autocomplete="off" autocomplete="cc-csc" value="123">)",
          /*hidden=*/false),
      R"(<input value="" AUTOCOMPLETE="section-pay CC-Number">)"
      R"(<input autocomplete="name" value="Ada">)"
      R"(<input autocomplete="off" autocomplete="cc-csc" value="123">)");

  base::DictValue attributes = *base::JSONReader::ReadDict(
      R"({"attributes": ["value", "4111", "autocomplete", "cc-number"]})",
      base::JSON_PARSE_RFC);
  RedactSecretFields("DOM.getAttributes", attributes, {}, false);
  EXPECT_EQ(base::WriteJson(attributes)->find("4111"), std::string::npos);

  base::DictValue node = *base::JSONReader::ReadDict(
      R"({"node": {"nodeName": "INPUT", "backendNodeId": 9,
           "attributes": ["type", "tel", "autocomplete", "cc-exp",
                          "value", "08/29"]}})",
      base::JSON_PARSE_RFC);
  RedactSecretFields("DOM.describeNode", node, {}, false);
  EXPECT_EQ(base::WriteJson(node)->find("08/29"), std::string::npos);

  // A readable tab's redaction hides them too.
  base::DictValue read = *base::JSONReader::ReadDict(
      R"({"attributes": ["autocomplete", "cc-number", "value", "4111"]})",
      base::JSON_PARSE_RFC);
  RedactSecretInputs("DOM.getAttributes", read);
  EXPECT_EQ(base::WriteJson(read)->find("4111"), std::string::npos);
}

}  // namespace
}  // namespace koan::agent
