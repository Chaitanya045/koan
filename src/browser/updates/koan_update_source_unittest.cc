// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/updates/koan_update_source.h"

#include <string>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::updates {
namespace {

// koan_update_logic_unittest.cc's throwaway key.
constexpr char kTestPublicKey[] =
    "KHTqhiTgjgu4WJI00c593Nn+Y2HFfnM/dKBsVh/jpLI=";

base::CommandLine WithSwitches() {
  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  command_line.AppendSwitchASCII(kManifestUrlSwitch,
                                 "http://127.0.0.1:8000/koan-update.json");
  command_line.AppendSwitchASCII(kPublicKeySwitch, kTestPublicKey);
  return command_line;
}

TEST(KoanUpdateSourceTest, BuildsTheReleaseKeyIn) {
  base::FilePath root;
  ASSERT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  std::string text;
  ASSERT_TRUE(base::ReadFileToString(
      root.AppendASCII("koan/browser/updates/koan_update_public_key.txt"),
      &text));
  EXPECT_EQ(BuiltInPublicKey(), ParsePublicKey(text));
}

TEST(KoanUpdateSourceTest, TrustsOnlyTheReleasesByDefault) {
  const UpdateSource source =
      GetUpdateSource(base::CommandLine(base::CommandLine::NO_PROGRAM));
  EXPECT_EQ(source.manifest_url, GURL(kManifestUrl));
  EXPECT_EQ(source.public_key, BuiltInPublicKey());
  EXPECT_FALSE(source.overridden);
}

TEST(KoanUpdateSourceTest, TestSwitchesNeedTheTestHooksBuild) {
  const UpdateSource source = GetUpdateSource(WithSwitches());
  if (!UpdateTestHooksBuilt()) {
    // What people install: the switches do nothing.
    EXPECT_EQ(source.manifest_url, GURL(kManifestUrl));
    EXPECT_EQ(source.public_key, BuiltInPublicKey());
    EXPECT_FALSE(source.overridden);
    return;
  }
  EXPECT_EQ(source.manifest_url.spec(),
            "http://127.0.0.1:8000/koan-update.json");
  EXPECT_EQ(source.public_key, ParsePublicKey(kTestPublicKey));
  EXPECT_TRUE(source.overridden);
}

TEST(KoanUpdateSourceTest, BadTestSwitchesKeepTheReleases) {
  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  command_line.AppendSwitchASCII(kManifestUrlSwitch, "file:///tmp/x.json");
  command_line.AppendSwitchASCII(kPublicKeySwitch, "AAAA");
  const UpdateSource source = GetUpdateSource(command_line);
  EXPECT_EQ(source.manifest_url, GURL(kManifestUrl));
  EXPECT_EQ(source.public_key, BuiltInPublicKey());
  EXPECT_FALSE(source.overridden);
}

}  // namespace
}  // namespace koan::updates
