// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/agent/koan_agent_upload_logic.h"

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "net/base/mime_util.h"

namespace koan::agent {

namespace {

// The accept list as the error shows it: ".pdf, image/*".
std::string AcceptText(const std::vector<std::string>& accept_types) {
  return base::JoinString(accept_types, ", ");
}

}  // namespace

std::optional<UploadRefusal> CheckChooser(ChooserMode mode, size_t count) {
  switch (mode) {
    case ChooserMode::kSave:
      return UploadRefusal{"Koan.notPermitted",
                           "the page asks where to save a file, not for one "
                           "to upload; koan upload only answers an open "
                           "dialog"};
    case ChooserMode::kFolder:
      return UploadRefusal{
          "Koan.notSupported",
          "the page asks for a folder (webkitdirectory); koan upload takes "
          "files only, not folders, yet. Ask the user to choose the folder "
          "(Open in Koan's indicator)"};
    case ChooserMode::kOpen:
      if (count != 1) {
        return UploadRefusal{
            "Koan.tooManyFiles",
            "the page's file input takes one file (no `multiple`); give one "
            "path"};
      }
      return std::nullopt;
    case ChooserMode::kOpenMultiple:
      if (count > kMaxUploadFiles) {
        return UploadRefusal{"Koan.tooManyFiles",
                             "one upload takes at most " +
                                 base::NumberToString(kMaxUploadFiles) +
                                 " files"};
      }
      return std::nullopt;
  }
  return std::nullopt;
}

AcceptMatch MatchAccept(const std::vector<std::string>& accept_types,
                        std::string_view file_name) {
  std::string extension =
      base::FilePath::FromUTF8Unsafe(file_name).FinalExtension();
  if (!extension.empty()) {
    extension = base::ToLowerASCII(extension.substr(1));
  }
  std::string mime;
  const bool known = !extension.empty() &&
                     net::GetWellKnownMimeTypeFromExtension(extension, &mime);
  bool checked = false;
  bool unsure = false;
  for (const std::string& type : accept_types) {
    if (type.size() > 1 && type.front() == '.') {
      checked = true;
      // ".tar.gz" names more than the final extension.
      if (file_name.size() > type.size() &&
          base::EndsWith(file_name, type,
                         base::CompareCase::INSENSITIVE_ASCII)) {
        return AcceptMatch::kMatches;
      }
    } else if (type.find('/') != std::string::npos) {
      checked = true;
      if (!known) {
        unsure = true;
      } else if (net::MatchesMimeType(type, mime)) {
        return AcceptMatch::kMatches;
      }
    }
  }
  if (!checked) {
    return AcceptMatch::kMatches;
  }
  return unsure ? AcceptMatch::kUnsure : AcceptMatch::kMismatch;
}

std::optional<UploadRefusal> CheckAccept(
    const std::vector<std::string>& accept_types,
    const std::vector<std::string>& names) {
  for (const std::string& name : names) {
    if (MatchAccept(accept_types, name) == AcceptMatch::kMismatch) {
      return UploadRefusal{
          "Koan.notAccepted",
          "the page's file input accepts " + AcceptText(accept_types) +
              ", and " + name +
              " is none of those; attach a file of that type (convert it "
              "first if you must)"};
    }
  }
  return std::nullopt;
}

bool IsPlainFileName(std::string_view name) {
  return !name.empty() && name.size() <= 255 && name != "." && name != ".." &&
         name.find('/') == std::string_view::npos &&
         name.find('\\') == std::string_view::npos &&
         name.find('\0') == std::string_view::npos && base::IsStringUTF8(name);
}

std::vector<base::FilePath> RefusedHomeFolders(const base::FilePath& home) {
  std::vector<base::FilePath> folders;
  if (home.empty()) {
    return folders;
  }
  for (const char* folder :
       {".koan", ".ssh", ".gnupg", "Library/Keychains", "Library/Cookies",
        ".aws", ".azure", ".config/gcloud", ".config/gh", ".docker", ".kube",
        ".git-credentials", ".netrc", ".npmrc", ".pypirc"}) {
    folders.push_back(home.Append(folder));
  }
#if BUILDFLAG(IS_LINUX)
  for (const char* folder : {
           // GNOME Keyring, KWallet (Plasma 5 and 6, and KDE 4's), pass.
           ".local/share/keyrings",
           ".local/share/kwalletd",
           ".kde/share/apps/kwallet",
           ".kde4/share/apps/kwallet",
           ".password-store",
           // The NSS database Chromium and others keep certificates and
           // client keys in.
           ".pki",
           // Other browsers' profiles: their cookies and saved passwords.
           ".mozilla",
           ".config/mozilla",
           "snap/firefox",
           ".librewolf",
           ".config/librewolf",
           ".zen",
           ".config/zen",
           ".floorp",
           ".config/floorp",
           ".waterfox",
           ".config/waterfox",
           ".thunderbird",
           ".config/google-chrome",
           ".config/google-chrome-beta",
           ".config/google-chrome-unstable",
           ".config/chromium",
           ".config/BraveSoftware",
           ".config/microsoft-edge",
           ".config/vivaldi",
           ".config/opera",
           // Every Flatpak app's own data, Koan's Flatpak profile with it.
           ".var/app",
       }) {
    folders.push_back(home.Append(folder));
  }
#endif
  return folders;
}

bool IsUnderAny(const base::FilePath& path,
                const std::vector<base::FilePath>& roots) {
  for (const base::FilePath& root : roots) {
    if (!root.empty() && (root == path || root.IsParent(path))) {
      return true;
    }
  }
  return false;
}

std::string SizeText(int64_t bytes) {
  if (bytes < 1024) {
    return base::StrCat({base::NumberToString(bytes), " B"});
  }
  const double kb = bytes / 1024.0;
  if (kb < 1024) {
    return base::StringPrintf("%.1f KB", kb);
  }
  const double mb = kb / 1024.0;
  if (mb < 1024) {
    return base::StringPrintf("%.1f MB", mb);
  }
  return base::StringPrintf("%.1f GB", mb / 1024.0);
}

std::u16string AttachedText(const std::vector<std::string>& names) {
  if (names.empty()) {
    return std::u16string();
  }
  const std::u16string first = base::UTF8ToUTF16(names.front());
  if (names.size() == 1) {
    return first;
  }
  if (names.size() == 2) {
    return base::StrCat({first, u" and ", base::UTF8ToUTF16(names[1])});
  }
  return base::StrCat({first, u" and ",
                       base::NumberToString16(names.size() - 1),
                       u" more files"});
}

std::string UploadSummary(const std::vector<std::string>& names,
                          const std::vector<int64_t>& sizes) {
  std::vector<std::string> parts;
  for (size_t i = 0; i < names.size(); ++i) {
    parts.push_back(i < sizes.size() ? base::StrCat({names[i], " (",
                                                     SizeText(sizes[i]), ")"})
                                     : names[i]);
  }
  return base::JoinString(parts, ", ");
}

}  // namespace koan::agent
