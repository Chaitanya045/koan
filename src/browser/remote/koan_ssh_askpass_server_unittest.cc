// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/remote/koan_ssh_askpass_server.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_view_util.h"
#include "base/test/task_environment.h"
#include "base/test/test_timeouts.h"
#include "base/threading/thread_restrictions.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::remote {
namespace {

using ::testing::UnorderedElementsAre;

// Runs the real helper script with /bin/sh, as ssh would (argv[1] is the
// prompt, SSH_ASKPASS_PROMPT the hint), in a process group of its own.
class Helper {
 public:
  Helper(const base::FilePath& helper_path,
         const std::string& prompt,
         const std::string& hint) {
    std::array<int, 2> out;
    CHECK_EQ(pipe(out.data()), 0);
    stdout_.reset(out[0]);
    base::ScopedFD write_end(out[1]);
    base::LaunchOptions options;
    options.fds_to_remap = {{write_end.get(), STDOUT_FILENO}};
    options.new_process_group = true;
    options.environment["SSH_ASKPASS_PROMPT"] = hint;
    process_ =
        base::LaunchProcess({"/bin/sh", helper_path.value(), prompt}, options);
    CHECK(process_.IsValid());
  }

  pid_t pid() const { return process_.Pid(); }

  // Waits for the helper's exit; its stdout and exit code.
  std::pair<std::string, int> Finish() {
    base::ScopedAllowBaseSyncPrimitivesForTesting allow_wait;
    int exit_code = -1;
    EXPECT_TRUE(process_.WaitForExitWithTimeout(TestTimeouts::action_timeout(),
                                                &exit_code));
    base::ScopedAllowBlockingForTesting allow_blocking;
    std::string output;
    std::array<char, 256> chunk;
    ssize_t n;
    while ((n = HANDLE_EINTR(read(stdout_.get(), chunk.data(), chunk.size()))) >
           0) {
      output.append(base::as_string_view(
          base::span(chunk).first(static_cast<size_t>(n))));
    }
    return {output, exit_code};
  }

 private:
  base::ScopedFD stdout_;
  base::Process process_;
};

class KoanSshAskpassServerTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    ASSERT_TRUE(base::SetPosixFilePermissions(dir_.GetPath(), 0700));
    server_ = SshAskpassServer::Create(
        dir_.GetPath(),
        base::BindRepeating(&KoanSshAskpassServerTest::OnRequest,
                            base::Unretained(this)));
    ASSERT_TRUE(server_);
  }

  void OnRequest(int id, SshPrompt prompt) {
    requests_.push_back({id, std::move(prompt)});
    if (quit_) {
      std::move(quit_).Run();
    }
  }

  // Runs until a request arrives.
  std::pair<int, SshPrompt> NextRequest() {
    if (requests_.empty()) {
      base::RunLoop run_loop;
      quit_ = run_loop.QuitClosure();
      run_loop.Run();
    }
    std::pair<int, SshPrompt> request = std::move(requests_.front());
    requests_.erase(requests_.begin());
    return request;
  }

  std::vector<std::string> DirEntries() {
    std::vector<std::string> names;
    base::FileEnumerator files(dir_.GetPath(), false,
                               base::FileEnumerator::FILES);
    for (base::FilePath file = files.Next(); !file.empty();
         file = files.Next()) {
      names.push_back(file.BaseName().value());
    }
    return names;
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};
  base::ScopedTempDir dir_;
  std::unique_ptr<SshAskpassServer> server_;
  std::vector<std::pair<int, SshPrompt>> requests_;
  base::OnceClosure quit_;
};

TEST_F(KoanSshAskpassServerTest, SetsUpAPrivateDirectory) {
  struct stat info;
  ASSERT_EQ(stat(server_->helper_path().value().c_str(), &info), 0);
  EXPECT_EQ(info.st_mode & 0777, 0700u);
  ASSERT_EQ(stat(dir_.GetPath().Append("ctl").value().c_str(), &info), 0);
  EXPECT_TRUE(S_ISFIFO(info.st_mode));
  EXPECT_EQ(info.st_mode & 0777, 0600u);
}

TEST_F(KoanSshAskpassServerTest, RefusesADirectoryOthersCanRead) {
  base::ScopedTempDir open_dir;
  ASSERT_TRUE(open_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::SetPosixFilePermissions(open_dir.GetPath(), 0755));
  EXPECT_FALSE(SshAskpassServer::Create(open_dir.GetPath(), base::DoNothing()));
}

TEST_F(KoanSshAskpassServerTest, PassesASecretToSsh) {
  Helper helper(server_->helper_path(), "me@box's password: ", "");
  server_->SetProcessGroup(helper.pid());
  auto [id, prompt] = NextRequest();
  EXPECT_EQ(id, helper.pid());
  EXPECT_EQ(prompt.kind, SshPromptKind::kSecret);
  EXPECT_EQ(prompt.text, "me@box's password: ");
  // The request file is gone as soon as the browser has read it.
  EXPECT_FALSE(
      base::PathExists(dir_.GetPath().Append("q." + base::NumberToString(id))));

  server_->Reply(id, SshPromptReply::WithSecret(SshSecret("hunter2 two")));
  auto [output, exit_code] = helper.Finish();
  EXPECT_EQ(output, "hunter2 two\n");
  EXPECT_EQ(exit_code, 0);
  // Nothing of the request is left behind.
  EXPECT_THAT(DirEntries(), UnorderedElementsAre("askpass", "ctl"));
}

TEST_F(KoanSshAskpassServerTest, ACancelIsNoAnswer) {
  Helper helper(server_->helper_path(), "Verification code: ", "");
  server_->SetProcessGroup(helper.pid());
  auto [id, prompt] = NextRequest();
  server_->Reply(id, SshPromptReply::Cancel());
  auto [output, exit_code] = helper.Finish();
  EXPECT_EQ(output, "");
  EXPECT_EQ(exit_code, 1);
}

TEST_F(KoanSshAskpassServerTest, AnswersQuestionsWithWords) {
  Helper helper(server_->helper_path(), "Allow use of key?", "confirm");
  server_->SetProcessGroup(helper.pid());
  auto [id, prompt] = NextRequest();
  EXPECT_EQ(prompt.kind, SshPromptKind::kConfirm);
  server_->Reply(id, SshPromptReply::No());
  auto [output, exit_code] = helper.Finish();
  EXPECT_EQ(output, "no\n");
  EXPECT_EQ(exit_code, 0);
}

TEST_F(KoanSshAskpassServerTest, RejectAllEndsOpenHelpers) {
  Helper helper(server_->helper_path(), "password: ", "");
  server_->SetProcessGroup(helper.pid());
  NextRequest();
  server_->RejectAll();
  auto [output, exit_code] = helper.Finish();
  EXPECT_EQ(output, "");
  EXPECT_EQ(exit_code, 1);
}

TEST_F(KoanSshAskpassServerTest, IgnoresProcessesOutsideTheGroup) {
  Helper helper(server_->helper_path(), "password: ", "");
  // Not the helper's group.
  server_->SetProcessGroup(getpgrp());
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    // A request that names this test process.
    base::ScopedFD control(
        open(dir_.GetPath().Append("ctl").value().c_str(), O_WRONLY));
    ASSERT_TRUE(control.is_valid());
    ASSERT_TRUE(base::WriteFileDescriptor(
        control.get(), base::NumberToString(getpid()) + "\n"));
  }
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(requests_.empty());
  // The helper is left waiting; ssh would kill it with the group.
  kill(helper.pid(), SIGTERM);
  auto [output, exit_code] = helper.Finish();
  EXPECT_EQ(output, "");
  EXPECT_TRUE(requests_.empty());
}

}  // namespace
}  // namespace koan::remote
