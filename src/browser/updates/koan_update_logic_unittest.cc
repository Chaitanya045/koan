// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/updates/koan_update_logic.h"

#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace koan::updates {
namespace {

// Test vectors: a throwaway Ed25519 key (never a release's), in
// koan_update_public_key.txt's format, and a manifest that
// scripts/ci/update_manifest.py made with it:
//
//   openssl genpkey -algorithm ed25519 -out test-key.pem
//   update_manifest.py make --platform mac-arm64 --file Koan.dmg \
//     --url
//     https://github.com/koan-browser/koan/releases/download/v0.0.3/Koan-0.0.3-arm64.dmg
//     \
//     --version 0.0.3 --chromium 158.0.8127.18 \
//     --notes-url https://github.com/koan-browser/koan/releases/tag/v0.0.3 \
//     --key-file test-key.pem --output koan-update.json
//   (and the same for linux-x86_64 with Koan.flatpak and
//   .../v0.0.3/Koan-0.0.3-x86_64.flatpak)
constexpr char kTestPublicKey[] =
    "KHTqhiTgjgu4WJI00c593Nn+Y2HFfnM/dKBsVh/jpLI=\n";

constexpr char kManifest[] = R"({
  "chromium": "158.0.8127.18",
  "notes_url": "https://github.com/koan-browser/koan/releases/tag/v0.0.3",
  "platforms": {
    "linux-x86_64": {
      "sha256": "f2c317484547ebaad146c9f05118963fd52d522a389087493ad537deaefba49e",
      "signature": "DXgqUYVGClU9XN86pLDZlgt8utNTaD5FgkytSDc3pgbMdWBhRNExKuyBRVmUlYMhiNUlDK6QYFNm4ObZW583Bg==",
      "size": 18,
      "url": "https://github.com/koan-browser/koan/releases/download/v0.0.3/Koan-0.0.3-x86_64.flatpak"
    },
    "mac-arm64": {
      "sha256": "f69b66205beddb182db65c2039d9c63aaebbd64130ad8a222b0cf22a40bfac70",
      "signature": "0JGh0hG8srboz3fZDE9vpxuE5W1NjYosqfl4XQzq1zO85Xnp1B+WR5h/AxKyoWyyz9HoG3Do1ZK+kvLOcMQNAQ==",
      "size": 16,
      "url": "https://github.com/koan-browser/koan/releases/download/v0.0.3/Koan-0.0.3-arm64.dmg"
    }
  },
  "schema": 1,
  "version": "0.0.3"
}
)";

// A pre-release past int's range in size, signed over
// `update_manifest.py message --platform mac-arm64 --version 0.0.4-alpha.1
// --sha256 0123...cdef --size 3000000000` with the same key.
constexpr char kBigSha256[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
constexpr char kBigSignature[] =
    "iG9wAKSD7DEvdew7gbObXkzCqSFZoC/mvYh3MDkmt8ze2pICypIPLPzjz/n8KTTE2JKpasLKlz"
    "Fuj/sH8+hDBA==";

// The real key's text (koan_update_public_key.txt). Here only to check that
// a test manifest doesn't verify with it; the app builds the key in from the
// file (koan_update_source_unittest.cc checks that).
constexpr char kReleasePublicKey[] =
    "9L/DeJJ6+uRRzg8UVhYmAO1G9E3OX977m+ZvJBtu3dY=";

PublicKey TestKey() {
  return ParsePublicKey(kTestPublicKey).value();
}

base::expected<UpdateInfo, ManifestError> Parse(
    std::string_view json,
    std::string_view platform = kPlatformMacArm64) {
  return ParseManifest(json, platform, TestKey());
}

// kManifest with `edit` applied.
std::string Edited(base::OnceCallback<void(base::DictValue&)> edit) {
  base::Value value =
      base::JSONReader::Read(kManifest, base::JSON_PARSE_RFC).value();
  std::move(edit).Run(value.GetDict());
  return base::WriteJson(value).value();
}

ManifestError ErrorOf(std::string_view json,
                      std::string_view platform = kPlatformMacArm64) {
  const base::expected<UpdateInfo, ManifestError> result =
      Parse(json, platform);
  EXPECT_FALSE(result.has_value()) << json;
  return result.has_value() ? ManifestError::kNotJson : result.error();
}

TEST(KoanUpdateLogicTest, OrdersVersionsBySemVer) {
  // Ascending: the issue's examples, then SemVer 2.0's.
  const std::vector<std::string_view> ascending = {
      "0.0.1",
      "0.0.2-alpha.1",
      "0.0.2-alpha.2",
      "0.0.2-beta.1",
      "0.0.2-rc.1",
      "0.0.2",
      "0.0.3-alpha.1",
      "0.0.3",
      "0.0.10",
      "0.1.0",
      "1.0.0-alpha",
      "1.0.0-alpha.1",
      "1.0.0-alpha.beta",
      "1.0.0-beta",
      "1.0.0-beta.2",
      "1.0.0-beta.11",
      "1.0.0-rc.1",
      "1.0.0",
      "2.0.0",
      "10.0.0",
  };
  for (size_t i = 0; i < ascending.size(); ++i) {
    for (size_t j = 0; j < ascending.size(); ++j) {
      const std::optional<int> result =
          CompareVersions(ascending[i], ascending[j]);
      ASSERT_TRUE(result) << ascending[i] << " " << ascending[j];
      EXPECT_EQ(*result < 0, i < j) << ascending[i] << " < " << ascending[j];
      EXPECT_EQ(*result == 0, i == j) << ascending[i] << " = " << ascending[j];
      EXPECT_EQ(IsNewerVersion(ascending[i], ascending[j]), i > j);
    }
  }
  // Build metadata doesn't count.
  EXPECT_EQ(CompareVersions("1.0.0+build.1", "1.0.0+build.2"), 0);
  EXPECT_EQ(CompareVersions("1.0.0-rc.1+x", "1.0.0-rc.1"), 0);
  // Numbers of any size, compared as numbers.
  EXPECT_TRUE(IsNewerVersion("0.0.100000000000000000000", "0.0.99"));
}

TEST(KoanUpdateLogicTest, RejectsWhatIsNotAVersion) {
  for (std::string_view text :
       {"", "1", "1.0", "1.0.0.0", "v1.0.0", " 1.0.0", "1.0.0 ", "01.0.0",
        "1.00.0", "1.0.0-", "1.0.0-01", "1.0.0-a..b", "1.0.0-a_b", "1.0.0+",
        "1.0.0+a..b", "-1.0.0", "1.0.0-alpha.1\n"}) {
    EXPECT_FALSE(IsValidVersion(text)) << text;
    EXPECT_FALSE(CompareVersions(text, "1.0.0")) << text;
    EXPECT_FALSE(IsNewerVersion(text, "0.0.1")) << text;
    EXPECT_FALSE(IsNewerVersion("9.9.9", text)) << text;
  }
  EXPECT_TRUE(IsValidVersion("1.0.0-0a.1"));
  EXPECT_TRUE(IsValidVersion("1.0.0-x-y.0+001"));
}

TEST(KoanUpdateLogicTest, ParsesPublicKeys) {
  // Both keys, in koan_update_public_key.txt's format.
  EXPECT_TRUE(ParsePublicKey(kTestPublicKey));
  const std::optional<PublicKey> release = ParsePublicKey(kReleasePublicKey);
  ASSERT_TRUE(release);
  EXPECT_EQ((*release)[0], 0xf4);
  EXPECT_EQ((*release)[31], 0xd6);
  EXPECT_NE(*release, TestKey());
  // Not one key.
  EXPECT_FALSE(ParsePublicKey(""));
  EXPECT_FALSE(ParsePublicKey("not base64!"));
  EXPECT_FALSE(ParsePublicKey("AAAA"));
  EXPECT_FALSE(ParsePublicKey(kBigSignature));
}

TEST(KoanUpdateLogicTest, SignsWhatUpdateManifestPySigns) {
  // `update_manifest.py message --platform mac-arm64 --version 0.0.3
  // --sha256 f69b...ac70 --size 16`, byte for byte.
  EXPECT_EQ(SignedMessage(kPlatformMacArm64, "0.0.3",
                          "f69b66205beddb182db65c2039d9c63aaebbd64130ad8a222b0c"
                          "f22a40bfac70",
                          16),
            "koan-update-v1\nmac-arm64\n0.0.3\n"
            "f69b66205beddb182db65c2039d9c63aaebbd64130ad8a222b0cf22a40bfac70\n"
            "16\n");
}

TEST(KoanUpdateLogicTest, ParsesASignedManifest) {
  const base::expected<UpdateInfo, ManifestError> dmg = Parse(kManifest);
  ASSERT_TRUE(dmg.has_value());
  EXPECT_EQ(dmg->version, "0.0.3");
  EXPECT_EQ(dmg->platform, kPlatformMacArm64);
  EXPECT_EQ(dmg->size, 16);
  EXPECT_EQ(dmg->sha256,
            "f69b66205beddb182db65c2039d9c63aaebbd64130ad8a222b0cf22a40bfac70");
  EXPECT_EQ(dmg->url.spec(),
            "https://github.com/koan-browser/koan/releases/download/v0.0.3/"
            "Koan-0.0.3-arm64.dmg");
  EXPECT_EQ(dmg->notes_url.spec(),
            "https://github.com/koan-browser/koan/releases/tag/v0.0.3");
  EXPECT_EQ(dmg->chromium, "158.0.8127.18");

  const base::expected<UpdateInfo, ManifestError> flatpak =
      Parse(kManifest, kPlatformLinuxX8664);
  ASSERT_TRUE(flatpak.has_value());
  EXPECT_EQ(flatpak->platform, kPlatformLinuxX8664);
  EXPECT_EQ(flatpak->size, 18);
  EXPECT_EQ(flatpak->url.ExtractFileName(), "Koan-0.0.3-x86_64.flatpak");
}

TEST(KoanUpdateLogicTest, ParsesBigSizesAndPreReleases) {
  const std::string json = Edited(base::BindOnce([](base::DictValue& m) {
    m.Set("version", "0.0.4-alpha.1");
    base::DictValue& entry = *m.FindDictByDottedPath("platforms.mac-arm64");
    entry.Set("sha256", kBigSha256);
    entry.Set("size", 3000000000.0);
    entry.Set("signature", kBigSignature);
  }));
  const base::expected<UpdateInfo, ManifestError> update = Parse(json);
  ASSERT_TRUE(update.has_value()) << static_cast<int>(update.error());
  EXPECT_EQ(update->version, "0.0.4-alpha.1");
  EXPECT_EQ(update->size, 3000000000);
}

TEST(KoanUpdateLogicTest, IgnoresMalformedManifests) {
  EXPECT_EQ(ErrorOf(""), ManifestError::kNotJson);
  EXPECT_EQ(ErrorOf("not json"), ManifestError::kNotJson);
  EXPECT_EQ(ErrorOf("[]"), ManifestError::kNotJson);
  // GitHub's 404 page, were it ever served with a 200.
  EXPECT_EQ(ErrorOf("<!DOCTYPE html><html></html>"), ManifestError::kNotJson);
  // A trailing comma: not RFC JSON.
  EXPECT_EQ(ErrorOf(R"({"schema": 1,})"), ManifestError::kNotJson);

  using Edit = void (*)(base::DictValue&);
  const Edit malformed[] = {
      [](base::DictValue& m) { m.Remove("version"); },
      [](base::DictValue& m) { m.Set("version", 3); },
      [](base::DictValue& m) { m.Set("version", "v0.0.3"); },
      [](base::DictValue& m) { m.Set("version", "0.0.3 "); },
      [](base::DictValue& m) { m.Remove("notes_url"); },
      [](base::DictValue& m) { m.Remove("platforms"); },
      [](base::DictValue& m) { m.Set("platforms", "mac-arm64"); },
      [](base::DictValue& m) { m.Set("chromium", 158); },
      [](base::DictValue& m) { m.SetByDottedPath("platforms.mac-arm64", 1); },
      [](base::DictValue& m) {
        m.FindDictByDottedPath("platforms.mac-arm64")->Remove("url");
      },
      [](base::DictValue& m) {
        m.FindDictByDottedPath("platforms.mac-arm64")->Remove("size");
      },
      [](base::DictValue& m) {
        m.SetByDottedPath("platforms.mac-arm64.size", -16);
      },
      [](base::DictValue& m) {
        m.SetByDottedPath("platforms.mac-arm64.size", 16.5);
      },
      [](base::DictValue& m) {
        m.SetByDottedPath("platforms.mac-arm64.size", "16");
      },
      [](base::DictValue& m) {
        m.SetByDottedPath("platforms.mac-arm64.size", true);
      },
      [](base::DictValue& m) {
        m.FindDictByDottedPath("platforms.mac-arm64")->Remove("sha256");
      },
      // Upper case, or not 64 digits.
      [](base::DictValue& m) {
        m.SetByDottedPath("platforms.mac-arm64.sha256",
                          "F69B66205BEDDB182DB65C2039D9C63AAEBBD64130AD8A222B0C"
                          "F22A40BFAC70");
      },
      [](base::DictValue& m) {
        m.SetByDottedPath("platforms.mac-arm64.sha256", "f69b6620");
      },
      [](base::DictValue& m) {
        m.FindDictByDottedPath("platforms.mac-arm64")->Remove("signature");
      },
      [](base::DictValue& m) {
        m.SetByDottedPath("platforms.mac-arm64.signature", "not base64!");
      },
      // 63 bytes.
      [](base::DictValue& m) {
        m.SetByDottedPath(
            "platforms.mac-arm64.signature",
            "0JGh0hG8srboz3fZDE9vpxuE5W1NjYosqfl4XQzq1zO85Xnp1B+WR5h/AxKyoWyyz9"
            "HoG3Do1ZK+kvLOcMQN");
      },
  };
  size_t i = 0;
  for (Edit edit : malformed) {
    EXPECT_EQ(ErrorOf(Edited(base::BindOnce(edit))), ManifestError::kMalformed)
        << "edit " << i++;
  }
}

TEST(KoanUpdateLogicTest, IgnoresUnknownSchemas) {
  EXPECT_EQ(ErrorOf(Edited(base::BindOnce(
                [](base::DictValue& m) { m.Remove("schema"); }))),
            ManifestError::kUnknownSchema);
  EXPECT_EQ(ErrorOf(Edited(base::BindOnce(
                [](base::DictValue& m) { m.Set("schema", 2); }))),
            ManifestError::kUnknownSchema);
  EXPECT_EQ(ErrorOf(Edited(base::BindOnce(
                [](base::DictValue& m) { m.Set("schema", "1"); }))),
            ManifestError::kUnknownSchema);
}

TEST(KoanUpdateLogicTest, IgnoresAManifestWithoutThePlatform) {
  const std::string mac_only = Edited(base::BindOnce([](base::DictValue& m) {
    m.FindDict("platforms")->Remove(kPlatformLinuxX8664);
  }));
  EXPECT_TRUE(Parse(mac_only).has_value());
  EXPECT_EQ(ErrorOf(mac_only, kPlatformLinuxX8664), ManifestError::kNoPlatform);
  EXPECT_EQ(ErrorOf(kManifest, "windows-x86_64"), ManifestError::kNoPlatform);
  // Another platform's broken entry doesn't matter.
  EXPECT_TRUE(Parse(Edited(base::BindOnce([](base::DictValue& m) {
                m.SetByDottedPath("platforms.linux-x86_64", "broken");
              }))).has_value());
}

TEST(KoanUpdateLogicTest, IgnoresForeignUrls) {
  for (std::string_view url : {
           "http://github.com/koan-browser/koan/releases/download/v0.0.3/"
           "Koan.dmg",
           "https://example.com/koan-browser/koan/releases/download/v0.0.3/"
           "Koan.dmg",
           "https://github.com.example.com/koan-browser/koan/releases/"
           "download/v0.0.3/Koan.dmg",
           "https://github.com/someone/koan/releases/download/v0.0.3/Koan.dmg",
           "https://github.com/koan-browser/koan-fork/releases/download/v0.0.3/"
           "Koan.dmg",
           "https://github.com/koan-browser/koan/archive/v0.0.3.zip",
           "https://github.com/koan-browser/koan/releases/download/",
           "https://github.com/koan-browser/koan/releases/download/Koan.dmg",
           "https://github.com/koan-browser/koan/releases/download/v0.0.3/",
           "https://github.com/koan-browser/koan/releases/download/v/x/y.dmg",
           "https://github.com/koan-browser/koan/releases/download/../../../"
           "evil/releases/download/v0.0.3/Koan.dmg",
           "https://github.com/koan-browser/koan/releases/download/v0.0.3/"
           "Koan.dmg?x=1",
           "https://github.com/koan-browser/koan/releases/download/v0.0.3/"
           "Koan.dmg#x",
           "https://github.com:8443/koan-browser/koan/releases/download/"
           "v0.0.3/Koan.dmg",
           "https://user@github.com/koan-browser/koan/releases/download/"
           "v0.0.3/Koan.dmg",
           "file:///koan-browser/koan/releases/download/v0.0.3/Koan.dmg",
           "not a url",
       }) {
    EXPECT_FALSE(IsReleaseDownloadUrl(GURL(url))) << url;
    const std::string json = Edited(base::BindOnce(
        [](const std::string& url, base::DictValue& m) {
          m.SetByDottedPath("platforms.mac-arm64.url", url);
        },
        std::string(url)));
    EXPECT_EQ(ErrorOf(json), ManifestError::kBadUrl) << url;
  }
  // GURL makes these the same URL as the release's.
  EXPECT_TRUE(IsReleaseDownloadUrl(
      GURL("https://GitHub.com/koan-browser/koan/releases/download/v0.0.3/"
           "Koan.dmg")));
  EXPECT_TRUE(IsReleaseDownloadUrl(
      GURL("https://github.com:443/koan-browser/koan/releases/download/v0.0.3/"
           "Koan.dmg")));

  for (std::string_view notes : {
           "https://example.com/koan-browser/koan/releases/tag/v0.0.3",
           "https://github.com/someone/koan/releases/tag/v0.0.3",
           "https://github.com/koan-browser/koan/releases/",
           "javascript:alert(1)",
       }) {
    EXPECT_FALSE(IsReleasesPageUrl(GURL(notes))) << notes;
    const std::string json = Edited(
        base::BindOnce([](const std::string& notes,
                          base::DictValue& m) { m.Set("notes_url", notes); },
                       std::string(notes)));
    EXPECT_EQ(ErrorOf(json), ManifestError::kBadUrl) << notes;
  }
}

TEST(KoanUpdateLogicTest, IgnoresWrongSignatures) {
  // With another key, the real one among them.
  EXPECT_EQ(ParseManifest(kManifest, kPlatformMacArm64,
                          ParsePublicKey(kReleasePublicKey).value())
                .error(),
            ManifestError::kBadSignature);
  using Edit = void (*)(base::DictValue&);
  const Edit tampered[] = {
      // A newer version on the same image: the version is signed.
      [](base::DictValue& m) { m.Set("version", "0.0.4"); },
      [](base::DictValue& m) {
        m.SetByDottedPath("platforms.mac-arm64.size", 17);
      },
      [](base::DictValue& m) {
        m.SetByDottedPath(
            "platforms.mac-arm64.sha256",
            "f69b66205beddb182db65c2039d9c63aaebbd64130ad8a222b0cf22a40bfac71");
      },
      // One platform's entry served as another's: the platform is signed.
      [](base::DictValue& m) {
        base::Value entry(
            m.FindDictByDottedPath("platforms.linux-x86_64")->Clone());
        entry.GetDict().Set("url",
                            "https://github.com/koan-browser/koan/releases/"
                            "download/v0.0.3/Koan-0.0.3-arm64.dmg");
        m.SetByDottedPath("platforms.mac-arm64", std::move(entry));
      },
      // A flipped bit.
      [](base::DictValue& m) {
        m.SetByDottedPath(
            "platforms.mac-arm64.signature",
            "1JGh0hG8srboz3fZDE9vpxuE5W1NjYosqfl4XQzq1zO85Xnp1B+WR5h/AxKyoWyyz9"
            "HoG3Do1ZK+kvLOcMQNAQ==");
      },
  };
  size_t i = 0;
  for (Edit edit : tampered) {
    EXPECT_EQ(ErrorOf(Edited(base::BindOnce(edit))),
              ManifestError::kBadSignature)
        << "edit " << i++;
  }
}

TEST(KoanUpdateLogicTest, NotifiesOncePerNewerVersion) {
  EXPECT_TRUE(ShouldNotify("0.0.3", "0.0.2-alpha.1", ""));
  EXPECT_TRUE(ShouldNotify("0.0.3", "0.0.2", "0.0.3-alpha.1"));
  // Shown for it already.
  EXPECT_FALSE(ShouldNotify("0.0.3", "0.0.2", "0.0.3"));
  // Not newer than the build.
  EXPECT_FALSE(ShouldNotify("0.0.3", "0.0.3", ""));
  EXPECT_FALSE(ShouldNotify("0.0.2", "0.0.3-alpha.1", ""));
  // releases/latest went back to an older one than was shown.
  EXPECT_FALSE(ShouldNotify("0.0.4", "0.0.3", "0.0.5"));
  // A stale pref from before is no version: as if never shown.
  EXPECT_TRUE(ShouldNotify("0.0.4", "0.0.3", "67585c575222"));
}

TEST(KoanUpdateLogicTest, ChecksAtMostEvery12Hours) {
  const base::Time now = base::Time::UnixEpoch() + base::Days(20000);
  EXPECT_EQ(kCheckInterval, base::Hours(12));
  EXPECT_EQ(NextCheckDelay(base::Time(), now), kFirstCheckDelay);
  EXPECT_EQ(NextCheckDelay(now - base::Hours(3), now), base::Hours(9));
  // Overdue: soon, not at once.
  EXPECT_EQ(NextCheckDelay(now - base::Days(3), now), kFirstCheckDelay);
  // A clock that went back.
  EXPECT_EQ(NextCheckDelay(now + base::Days(2), now), kCheckInterval);
}

TEST(KoanUpdateLogicTest, Urls) {
  EXPECT_EQ(GURL(kManifestUrl).spec(),
            "https://github.com/koan-browser/koan/releases/latest/download/"
            "koan-update.json");
  // Not a file of a release: the manifest URL is no download URL.
  EXPECT_FALSE(IsReleaseDownloadUrl(GURL(kManifestUrl)));
  EXPECT_EQ(ReleasePageUrl("0.0.3-alpha.1").spec(),
            "https://github.com/koan-browser/koan/releases/tag/v0.0.3-alpha.1");
  EXPECT_TRUE(IsReleasesPageUrl(ReleasePageUrl("0.0.3")));
  EXPECT_TRUE(ReleasePageUrl("../../evil").is_empty());
  EXPECT_EQ(IssuesUrl().spec(), "https://github.com/koan-browser/koan/issues");
  EXPECT_EQ(BuildStamp("0.0.3", "158.0.8127.18"), "0.0.3/158.0.8127.18");
}

TEST(KoanUpdateLogicTest, FlatpakInstallCommand) {
  const UpdateInfo flatpak = Parse(kManifest, kPlatformLinuxX8664).value();
  EXPECT_EQ(FlatpakInstallCommand(flatpak, "/home/ada/Downloads"),
            "flatpak install --user --reinstall "
            "/home/ada/Downloads/Koan-0.0.3-x86_64.flatpak");
  EXPECT_EQ(FlatpakInstallCommand(flatpak, "/home/ada/Downloads/"),
            "flatpak install --user --reinstall "
            "/home/ada/Downloads/Koan-0.0.3-x86_64.flatpak");
  // Quoted for the shell.
  EXPECT_EQ(FlatpakInstallCommand(flatpak, "/home/ada/My Downloads"),
            "flatpak install --user --reinstall "
            "'/home/ada/My Downloads/Koan-0.0.3-x86_64.flatpak'");
  EXPECT_EQ(FlatpakInstallCommand(flatpak, "/home/ada/it's $HOME"),
            "flatpak install --user --reinstall "
            "'/home/ada/it'\\''s $HOME/Koan-0.0.3-x86_64.flatpak'");
  EXPECT_FALSE(FlatpakInstallCommand(flatpak, ""));
  // Not a Flatpak, or not plainly named.
  EXPECT_FALSE(
      FlatpakInstallCommand(Parse(kManifest).value(), "/home/ada/Downloads"));
  UpdateInfo odd = flatpak;
  odd.url = GURL(
      "https://github.com/koan-browser/koan/releases/download/v0.0.3/"
      "Koan%20$(x).flatpak");
  EXPECT_FALSE(FlatpakInstallCommand(odd, "/home/ada/Downloads"));
}

TEST(KoanUpdateLogicTest, KnowsThisPlatform) {
#if BUILDFLAG(IS_MAC) && defined(ARCH_CPU_ARM64)
  EXPECT_EQ(CurrentPlatform(), kPlatformMacArm64);
#elif BUILDFLAG(IS_LINUX) && defined(ARCH_CPU_X86_64)
  EXPECT_EQ(CurrentPlatform(), kPlatformLinuxX8664);
#else
  EXPECT_FALSE(CurrentPlatform());
#endif
}

}  // namespace
}  // namespace koan::updates
