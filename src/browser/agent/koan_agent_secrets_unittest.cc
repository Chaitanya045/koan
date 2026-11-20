// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_secrets.h"

#include <algorithm>
#include <string>

#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::agent {
namespace {

// A message as the facade serializes it.
std::string Message(base::DictValue params) {
  base::DictValue message;
  message.Set("method", "Runtime.consoleAPICalled");
  message.Set("params", std::move(params));
  return *base::WriteJson(message);
}

bool ValidJson(const std::string& text) {
  return base::JSONReader::Read(text, base::JSON_PARSE_RFC).has_value();
}

TEST(KoanAgentSecretsTest, ReplacesTheValueAsTheWriterWritesIt) {
  SecretScrubber scrubber;
  ASSERT_TRUE(scrubber.Add("hunter2!"));
  std::string json = Message(base::DictValue().Set("text", "pw is hunter2!"));
  EXPECT_EQ(scrubber.Scrub(json), 1u);
  EXPECT_EQ(json.find("hunter2"), std::string::npos);
  EXPECT_NE(json.find("pw is [secret]"), std::string::npos);
  EXPECT_TRUE(ValidJson(json));
}

TEST(KoanAgentSecretsTest, EveryEncoding) {
  const std::string secret = "p\"a<ss w/rd+1";
  SecretScrubber scrubber;
  ASSERT_TRUE(scrubber.Add(secret));
  // Raw (JSON-escaped by the writer: \" and <), as JSON.stringify
  // writes it inside a string the page made, URL-encoded both ways and in
  // either hex case, and base64 at each alignment, standard and URL-safe.
  const std::string stringified = "p\\\"a<ss w/rd+1";
  const std::vector<std::string> carriers = {
      secret,
      base::StrCat({"{\"password\":\"", stringified, "\"}"}),
      "user=ada&password=p%22a%3Css+w%2Frd%2B1&x=1",
      "q=p%22a%3Css%20w%2Frd%2B1",
      "q=p%22a%3css%20w%2frd%2b1",
      base::Base64Encode(secret),
      base::Base64Encode("x" + secret + "tail"),
      base::Base64Encode("xy" + secret),
      base::Base64Encode("password=p%22a%3Css+w%2Frd%2B1"),
  };
  for (const std::string& carrier : carriers) {
    std::string json = Message(base::DictValue().Set("text", carrier));
    EXPECT_GE(scrubber.Scrub(json), 1u) << carrier;
    EXPECT_TRUE(ValidJson(json)) << json;
    EXPECT_NE(json.find(kSecretMark), std::string::npos) << carrier;
    // None of the secret's encodings is left.
    for (const std::string& needle : SecretNeedles(secret)) {
      EXPECT_EQ(json.find(needle), std::string::npos)
          << needle << " in " << json;
    }
  }
  // URL-safe base64.
  std::string url_safe = base::Base64Encode("\xfb\xff" + secret);
  std::string json = Message(base::DictValue().Set("text", url_safe));
  std::replace(json.begin(), json.end(), '+', '-');
  std::replace(json.begin(), json.end(), '/', '_');
  EXPECT_GE(scrubber.Scrub(json), 1u);
}

TEST(KoanAgentSecretsTest, ASecretInJsonInsideJson) {
  // A page's JSON.stringify'd object returned by Runtime.evaluate, then
  // serialized again by the facade: escaped twice.
  SecretScrubber scrubber;
  ASSERT_TRUE(scrubber.Add("s3cr\"et\\x"));
  base::DictValue inner;
  inner.Set("p", "s3cr\"et\\x");
  base::DictValue outer;
  outer.Set("result", base::DictValue().Set("value", *base::WriteJson(inner)));
  std::string json = *base::WriteJson(outer);
  EXPECT_GE(scrubber.Scrub(json), 1u);
  EXPECT_EQ(json.find("s3cr"), std::string::npos);
  EXPECT_TRUE(ValidJson(json)) << json;
}

TEST(KoanAgentSecretsTest, NeverLeavesAHalfEscape) {
  // A form of the secret starts with what follows a backslash in the text
  // (the writer's "\n" for a newline): the match takes the backslash with
  // it, so the message stays valid JSON.
  SecretScrubber scrubber;
  ASSERT_TRUE(scrubber.Add("n0123456"));
  std::string json = Message(base::DictValue().Set("text", "a\n0123456"));
  EXPECT_EQ(scrubber.Scrub(json), 1u);
  EXPECT_NE(json.find("a[secret]"), std::string::npos) << json;
  EXPECT_TRUE(ValidJson(json)) << json;
  // An occurrence of a base64 run that starts inside a \u escape.
  SecretScrubber hex;
  ASSERT_TRUE(hex.Add("003Cabcdef"));
  std::string text = Message(base::DictValue().Set("text", "<abcdef"));
  hex.Scrub(text);
  EXPECT_TRUE(ValidJson(text)) << text;
}

TEST(KoanAgentSecretsTest, ShortValuesAreNotLookedFor) {
  SecretScrubber scrubber;
  // A CVC.
  EXPECT_FALSE(scrubber.Add("123"));
  EXPECT_FALSE(scrubber.Add("12345"));
  EXPECT_TRUE(scrubber.empty());
  EXPECT_TRUE(SecretNeedles("12345").empty());
  std::string json = Message(base::DictValue().Set("text", "123 12345"));
  EXPECT_EQ(scrubber.Scrub(json), 0u);
  EXPECT_FALSE(scrubber.Add(""));
}

TEST(KoanAgentSecretsTest, NoFalsePositivesOnOrdinaryText) {
  SecretScrubber scrubber;
  ASSERT_TRUE(scrubber.Add("Tr0ub4dor&3"));
  // Base64 runs shorter than kMinBase64Needle are not looked for, so text
  // that shares a few characters with the encoding is left.
  for (const std::string& needle : SecretNeedles("Tr0ub4dor&3")) {
    EXPECT_GE(needle.size(), kMinSecretLength) << needle;
  }
  const std::string text =
      "Troubadour 3, VHIwdWI (a prefix of the base64 alone), Tr0ub4d0r&3";
  std::string json = Message(base::DictValue().Set("text", text));
  const std::string before = json;
  EXPECT_EQ(scrubber.Scrub(json), 0u);
  EXPECT_EQ(json, before);
}

TEST(KoanAgentSecretsTest, ManySecretsOnePass) {
  SecretScrubber scrubber;
  for (int i = 0; i < 50; ++i) {
    scrubber.Add(base::StrCat({"secret-", base::NumberToString(i), "-value"}));
  }
  EXPECT_EQ(scrubber.size(), 50u);
  std::string json = Message(base::DictValue().Set(
      "text", "secret-7-value and secret-42-value, not secret-77-valu"));
  EXPECT_EQ(scrubber.Scrub(json), 2u);
  EXPECT_NE(json.find("secret-77-valu"), std::string::npos);
  EXPECT_EQ(json.find("secret-42-value"), std::string::npos);
}

TEST(KoanAgentSecretsTest, OverlappingOccurrencesBecomeOneMark) {
  SecretScrubber scrubber;
  ASSERT_TRUE(scrubber.Add("abcabcabc"));
  ASSERT_TRUE(scrubber.Add("cabcab"));
  std::string json = Message(base::DictValue().Set("text", "xabcabcabcx"));
  EXPECT_EQ(scrubber.Scrub(json), 1u);
  EXPECT_NE(json.find("x[secret]x"), std::string::npos) << json;
}

TEST(KoanAgentSecretsTest, ALargeMessage) {
  SecretScrubber scrubber;
  ASSERT_TRUE(scrubber.Add("4242424242424242"));
  std::string body(4 * 1024 * 1024, 'a');
  body.replace(3 * 1024 * 1024, 16, "4242424242424242");
  std::string json = Message(base::DictValue().Set("postData", body));
  EXPECT_EQ(scrubber.Scrub(json), 1u);
  EXPECT_EQ(json.find("4242424242424242"), std::string::npos);
  EXPECT_TRUE(ValidJson(json));
}

TEST(KoanAgentSecretsTest, UnicodeValues) {
  SecretScrubber scrubber;
  ASSERT_TRUE(scrubber.Add("pässwörd"));
  std::string json = Message(base::DictValue().Set("text", "x pässwörd y"));
  EXPECT_EQ(scrubber.Scrub(json), 1u);
  std::string encoded =
      Message(base::DictValue().Set("text", "q=p%C3%A4ssw%C3%B6rd&x=1"));
  EXPECT_EQ(scrubber.Scrub(encoded), 1u);
  scrubber.Clear();
  EXPECT_TRUE(scrubber.empty());
  std::string after = Message(base::DictValue().Set("text", "pässwörd"));
  EXPECT_EQ(scrubber.Scrub(after), 0u);
}

}  // namespace
}  // namespace koan::agent
