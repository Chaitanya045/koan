// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/cli/koan_cli_upload.h"

#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "koan/cli/koan_cli_args.h"
#include "koan/cli/koan_cli_batch.h"
#include "koan/cli/koan_cli_protocol.h"
#include "koan/cli/koan_cli_skill.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::cli {
namespace {

Command Parse(std::vector<std::string> args) {
  ParseResult result = ParseArgs(args);
  EXPECT_TRUE(result.command) << result.error;
  return result.command.value_or(Command());
}

std::string ErrorOf(std::vector<std::string> args) {
  ParseResult result = ParseArgs(args);
  EXPECT_FALSE(result.command);
  return result.error;
}

class KoanCliUploadTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    // As the paths are compared: with links resolved (/tmp on the Mac).
    root_ = base::MakeAbsoluteFilePath(dir_.GetPath());
    ASSERT_TRUE(base::WriteFile(root_.Append("resume.pdf"), "%PDF-1.4"));
    ASSERT_TRUE(base::CreateDirectory(root_.Append("folder")));
  }

  base::ScopedTempDir dir_;
  base::FilePath root_;
};

TEST_F(KoanCliUploadTest, PathsAreResolvedAndChecked) {
  std::vector<UploadPath> paths;
  std::string error;
  // Relative to the current folder, and absolute.
  ASSERT_TRUE(
      ResolveUploadPaths({"resume.pdf", root_.Append("resume.pdf").value()},
                         root_, &paths, &error))
      << error;
  ASSERT_EQ(paths.size(), 2u);
  EXPECT_EQ(paths[0].path, root_.Append("resume.pdf").value());
  EXPECT_EQ(paths[0].name, "resume.pdf");
  EXPECT_EQ(paths[1].path, paths[0].path);

  // A link: the file it names is sent, under the link's name.
  ASSERT_TRUE(base::CreateSymbolicLink(root_.Append("resume.pdf"),
                                       root_.Append("Ada CV.pdf")));
  ASSERT_TRUE(ResolveUploadPaths({"Ada CV.pdf"}, root_, &paths, &error));
  EXPECT_EQ(paths[0].path, root_.Append("resume.pdf").value());
  EXPECT_EQ(paths[0].name, "Ada CV.pdf");
}

TEST_F(KoanCliUploadTest, ClearErrorsForWhatCannotBeUploaded) {
  std::vector<UploadPath> paths;
  std::string error;
  EXPECT_FALSE(ResolveUploadPaths({"missing.pdf"}, root_, &paths, &error));
  EXPECT_EQ(error.rfind("Koan.fileNotFound: no file at missing.pdf", 0), 0u)
      << error;
  EXPECT_NE(error.find("scp"), std::string::npos);
  EXPECT_FALSE(ResolveUploadPaths({"folder"}, root_, &paths, &error));
  EXPECT_EQ(error.rfind("Koan.notAFile: folder is a folder", 0), 0u) << error;
  EXPECT_FALSE(ResolveUploadPaths({}, root_, &paths, &error));
  EXPECT_FALSE(ResolveUploadPaths(std::vector<std::string>(33, "resume.pdf"),
                                  root_, &paths, &error));
  EXPECT_EQ(error.rfind("Koan.tooManyFiles", 0), 0u) << error;
  // The first one that fails is named.
  EXPECT_FALSE(
      ResolveUploadPaths({"resume.pdf", "gone.pdf"}, root_, &paths, &error));
  EXPECT_NE(error.find("gone.pdf"), std::string::npos);

  // Unreadable (not as root, who reads everything).
  const base::FilePath locked = root_.Append("locked.pdf");
  ASSERT_TRUE(base::WriteFile(locked, "x"));
  ASSERT_EQ(chmod(locked.value().c_str(), 0), 0);
  if (geteuid() != 0) {
    EXPECT_FALSE(ResolveUploadPaths({"locked.pdf"}, root_, &paths, &error));
    EXPECT_EQ(error.rfind("Koan.fileUnreadable: locked.pdf", 0), 0u) << error;
  }
  chmod(locked.value().c_str(), 0600);
}

TEST(KoanCliUploadParamsTest, ParamsAndAnswer) {
  const base::DictValue params = UploadParams(
      {{"/tmp/a/resume.pdf", "resume.pdf"}, {"/tmp/b.png", "b.png"}},
      kUploadWait);
  const base::ListValue* files = params.FindList("files");
  ASSERT_TRUE(files);
  ASSERT_EQ(files->size(), 2u);
  EXPECT_EQ(*(*files)[0].GetDict().FindString("path"), "/tmp/a/resume.pdf");
  EXPECT_EQ(*(*files)[1].GetDict().FindString("name"), "b.png");
  EXPECT_EQ(params.FindInt("waitMs"), 5000);

  base::DictValue result;
  result.Set("summary", "resume.pdf (120.4 KB)");
  EXPECT_EQ(UploadedLine(result), "ok: attached resume.pdf (120.4 KB)");
  EXPECT_EQ(UploadedLine(base::DictValue()), "ok: attached the files");
}

TEST(KoanCliUploadArgsTest, TheCommandAndTheStep) {
  Command upload = Parse({"upload", "ab12", "cv.pdf", "/tmp/cover letter.pdf"});
  EXPECT_EQ(upload.kind, CommandKind::kUpload);
  EXPECT_EQ(upload.tab, "ab12");
  EXPECT_EQ(upload.files,
            (std::vector<std::string>{"cv.pdf", "/tmp/cover letter.pdf"}));
  EXPECT_NE(ErrorOf({"upload", "ab12"}).find("usage: koan upload"),
            std::string::npos);

  // In a batch: the paths run up to the next verb or option.
  Command batch = Parse({"act", "ab12", "fill", "@3", "Ada", "upload", "@12",
                         "cv.pdf", "cover.pdf", "click", "@31", "--snapshot"});
  ASSERT_EQ(batch.steps.size(), 3u);
  EXPECT_EQ(batch.steps[1].act, ActKind::kUpload);
  EXPECT_EQ(batch.steps[1].target, "@12");
  EXPECT_EQ(batch.steps[1].files,
            (std::vector<std::string>{"cv.pdf", "cover.pdf"}));
  EXPECT_EQ(batch.steps[2].act, ActKind::kClick);
  EXPECT_TRUE(batch.snapshot_after);
  EXPECT_EQ(StepLabel(batch.steps[1]), "upload @12 cv.pdf cover.pdf");
  Step deep;
  deep.act = ActKind::kUpload;
  deep.target = "loc:role=button[name=\"Upload\"]";
  deep.files = {"/Users/<user>/Desktop/cv.pdf"};
  EXPECT_EQ(StepLabel(deep), "upload loc:role=button[name=\"Upload\"] cv.pdf");
  // The first step of a single upload is the command's.
  Command single = Parse({"act", "ab12", "upload", "@12", "cv.pdf"});
  EXPECT_EQ(single.act, ActKind::kUpload);
  EXPECT_EQ(single.files, (std::vector<std::string>{"cv.pdf"}));
  // No path, no target.
  EXPECT_NE(ErrorOf({"act", "ab12", "upload", "@12"}).find("usage: upload"),
            std::string::npos);
  EXPECT_NE(ErrorOf({"act", "ab12", "upload", "@12", "click", "@3"})
                .find("usage: upload"),
            std::string::npos);
  EXPECT_NE(ErrorOf({"act", "ab12", "upload", "cv.pdf"}).find("usage: upload"),
            std::string::npos);

  const std::optional<std::string> help = HelpText("upload");
  ASSERT_TRUE(help);
  for (const char* text : {"koan upload <tab>", "upload <target>", "scp",
                           "multiple", "accept", "Koan.noFileChooser"}) {
    EXPECT_NE(help->find(text), std::string::npos) << text;
  }
  EXPECT_NE(HelpText("act")->find("upload <target>"), std::string::npos);
  // The guide says how, and what may be sent.
  const std::string guide = SkillGuide("");
  for (const char* text : {"koan act <tab> upload @12", "koan upload <tab>",
                           "scp", "never a key"}) {
    EXPECT_NE(guide.find(text), std::string::npos) << text;
  }
}

// A file chooser the page holds stops a batch, saying how to answer it,
// except before an upload step, which answers it.
TEST(KoanCliUploadArgsTest, TheGateAndAFileChooser) {
  BatchGate gate("T1");
  gate.OnEvent(*ParseMessage(R"({"method":"Koan.userNeeded","params":{
      "targetId":"T1","reason":"file-picker"}})"));
  ASSERT_TRUE(gate.StopReason());
  EXPECT_EQ(gate.StopReason()->rfind("Koan.userNeeded: ", 0), 0u);
  EXPECT_NE(gate.StopReason()->find("koan upload T1 <path>"), std::string::npos)
      << *gate.StopReason();
  EXPECT_FALSE(gate.StopReason(/*answers_file_chooser=*/true));
  // Anything else still stops it.
  gate.OnEvent(*ParseMessage(R"({"method":"Koan.userNeeded","params":{
      "targetId":"T1","reason":"dialog"}})"));
  ASSERT_TRUE(gate.StopReason(/*answers_file_chooser=*/true));
  EXPECT_EQ(gate.StopReason(true)->find("file-picker"), std::string::npos);
  gate.OnEvent(*ParseMessage(R"({"method":"Koan.userNeededDone","params":{
      "targetId":"T1","reason":"dialog"}})"));
  gate.OnEvent(*ParseMessage(R"({"method":"Koan.userNeededDone","params":{
      "targetId":"T1","reason":"file-picker"}})"));
  EXPECT_FALSE(gate.StopReason());
}

}  // namespace
}  // namespace koan::cli
