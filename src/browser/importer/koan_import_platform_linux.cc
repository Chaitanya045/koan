// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/importer/koan_import_platform.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_view_util.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "dbus/bus.h"
#include "dbus/error.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"
#include "koan/browser/importer/koan_import_sources.h"

// Another browser's Safe Storage secret on Linux (#493): the item os_crypt
// keeps it in (freedesktop_secret_key_provider.h), looked up by its exact
// attributes over a private session bus connection, with blocking calls on
// the import's thread pool task. Koan's Flatpak may talk to the Secret
// Service and kwalletd5/6 already, for its own key (flatpak/dev.koan.Koan.yml).
namespace koan::importer {

namespace {

constexpr char kSecretService[] = "org.freedesktop.secrets";
constexpr char kSecretServicePath[] = "/org/freedesktop/secrets";
constexpr char kSecretServiceInterface[] = "org.freedesktop.Secret.Service";
constexpr char kSecretSessionInterface[] = "org.freedesktop.Secret.Session";
constexpr char kSecretItemInterface[] = "org.freedesktop.Secret.Item";
// The attributes of os_crypt's item.
constexpr char kSchemaAttribute[] = "xdg:schema";
constexpr char kSchema[] = "chrome_libsecret_os_crypt_password_v2";
constexpr char kApplicationAttribute[] = "application";

constexpr char kKWalletInterface[] = "org.kde.KWallet";
// Koan's name in KWallet's prompt and in its list of apps allowed.
constexpr char kKWalletAppId[] = "Koan";
// Opening the wallet may wait for the user to allow Koan.
constexpr int kKWalletOpenTimeoutMs = base::Minutes(2).InMilliseconds();

std::unique_ptr<dbus::Response> Call(
    dbus::ObjectProxy* proxy,
    dbus::MethodCall* call,
    int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) {
  base::expected<std::unique_ptr<dbus::Response>, dbus::Error> response =
      proxy->CallMethodAndBlock(call, timeout_ms);
  return response.has_value() ? std::move(response.value()) : nullptr;
}

// The secret of the first unlocked item with os_crypt's schema and
// `application`, in `session`. A locked item is left alone: Koan never asks
// to unlock a keyring for an import.
std::optional<std::string> ReadSecretItem(dbus::Bus* bus,
                                          const dbus::ObjectPath& session,
                                          std::string_view application) {
  dbus::ObjectProxy* service =
      bus->GetObjectProxy(kSecretService, dbus::ObjectPath(kSecretServicePath));
  dbus::MethodCall search(kSecretServiceInterface, "SearchItems");
  dbus::MessageWriter writer(&search);
  dbus::MessageWriter attributes(nullptr);
  writer.OpenArray("{ss}", &attributes);
  const std::pair<std::string_view, std::string_view> pairs[] = {
      {kSchemaAttribute, kSchema}, {kApplicationAttribute, application}};
  for (const auto& [name, value] : pairs) {
    dbus::MessageWriter entry(nullptr);
    attributes.OpenDictEntry(&entry);
    entry.AppendString(std::string(name));
    entry.AppendString(std::string(value));
    attributes.CloseContainer(&entry);
  }
  writer.CloseContainer(&attributes);
  std::unique_ptr<dbus::Response> found = Call(service, &search);
  std::vector<dbus::ObjectPath> unlocked;
  if (!found ||
      !dbus::MessageReader(found.get()).PopArrayOfObjectPaths(&unlocked) ||
      unlocked.empty()) {
    return std::nullopt;
  }

  dbus::MethodCall get(kSecretItemInterface, "GetSecret");
  dbus::MessageWriter(&get).AppendObjectPath(session);
  std::unique_ptr<dbus::Response> response =
      Call(bus->GetObjectProxy(kSecretService, unlocked.front()), &get);
  if (!response) {
    return std::nullopt;
  }
  // (oayays): the session, the parameters, the value, its type.
  dbus::MessageReader reader(response.get());
  dbus::MessageReader secret(nullptr);
  dbus::ObjectPath secret_session;
  base::span<const uint8_t> parameters;
  base::span<const uint8_t> value;
  if (!reader.PopStruct(&secret) || !secret.PopObjectPath(&secret_session) ||
      !secret.PopArrayOfBytes(&parameters) || !secret.PopArrayOfBytes(&value) ||
      value.empty()) {
    return std::nullopt;
  }
  return std::string(base::as_string_view(value));
}

// From the Secret Service (GNOME Keyring, KeePassXC, KWallet's bridge): in
// a plain session, as os_crypt reads Koan's own item.
std::optional<std::string> ReadSecretService(dbus::Bus* bus,
                                             std::string_view application) {
  if (bus->GetServiceOwnerAndBlock(kSecretService, dbus::Bus::SUPPRESS_ERRORS)
          .empty()) {
    return std::nullopt;
  }
  dbus::ObjectProxy* service =
      bus->GetObjectProxy(kSecretService, dbus::ObjectPath(kSecretServicePath));
  dbus::MethodCall open_session(kSecretServiceInterface, "OpenSession");
  dbus::MessageWriter writer(&open_session);
  writer.AppendString("plain");
  writer.AppendVariantOfString("");
  std::unique_ptr<dbus::Response> opened = Call(service, &open_session);
  if (!opened) {
    return std::nullopt;
  }
  dbus::MessageReader reader(opened.get());
  dbus::MessageReader output(nullptr);
  dbus::ObjectPath session;
  if (!reader.PopVariant(&output) || !reader.PopObjectPath(&session)) {
    return std::nullopt;
  }
  std::optional<std::string> secret = ReadSecretItem(bus, session, application);
  dbus::MethodCall close(kSecretSessionInterface, "Close");
  Call(bus->GetObjectProxy(kSecretService, session), &close);
  return secret;
}

// From KWallet (KDE), where Chromium keeps its secret on KDE: entry `key` of
// `folder` in the network wallet. Only a kwalletd that is running: never
// starts one.
std::optional<std::string> ReadKWallet(dbus::Bus* bus,
                                       std::string_view folder,
                                       std::string_view key) {
  static constexpr std::pair<const char*, const char*> kWallets[] = {
      {"org.kde.kwalletd6", "/modules/kwalletd6"},
      {"org.kde.kwalletd5", "/modules/kwalletd5"}};
  for (const auto& [service_name, path] : kWallets) {
    if (bus->GetServiceOwnerAndBlock(service_name, dbus::Bus::SUPPRESS_ERRORS)
            .empty()) {
      continue;
    }
    dbus::ObjectProxy* wallet =
        bus->GetObjectProxy(service_name, dbus::ObjectPath(path));
    dbus::MethodCall is_enabled(kKWalletInterface, "isEnabled");
    std::unique_ptr<dbus::Response> response = Call(wallet, &is_enabled);
    bool enabled = false;
    if (!response || !dbus::MessageReader(response.get()).PopBool(&enabled) ||
        !enabled) {
      return std::nullopt;
    }
    dbus::MethodCall network_wallet(kKWalletInterface, "networkWallet");
    response = Call(wallet, &network_wallet);
    std::string wallet_name;
    if (!response ||
        !dbus::MessageReader(response.get()).PopString(&wallet_name)) {
      return std::nullopt;
    }
    // KWallet asks the user the first time Koan opens the wallet.
    dbus::MethodCall open(kKWalletInterface, "open");
    dbus::MessageWriter open_writer(&open);
    open_writer.AppendString(wallet_name);
    open_writer.AppendInt64(0);
    open_writer.AppendString(kKWalletAppId);
    response = Call(wallet, &open, kKWalletOpenTimeoutMs);
    int32_t handle = -1;
    if (!response || !dbus::MessageReader(response.get()).PopInt32(&handle) ||
        handle < 0) {
      return std::nullopt;
    }

    std::optional<std::string> secret;
    dbus::MethodCall has_entry(kKWalletInterface, "hasEntry");
    dbus::MessageWriter has_writer(&has_entry);
    has_writer.AppendInt32(handle);
    has_writer.AppendString(std::string(folder));
    has_writer.AppendString(std::string(key));
    has_writer.AppendString(kKWalletAppId);
    response = Call(wallet, &has_entry);
    bool has = false;
    if (response && dbus::MessageReader(response.get()).PopBool(&has) && has) {
      dbus::MethodCall read(kKWalletInterface, "readPassword");
      dbus::MessageWriter read_writer(&read);
      read_writer.AppendInt32(handle);
      read_writer.AppendString(std::string(folder));
      read_writer.AppendString(std::string(key));
      read_writer.AppendString(kKWalletAppId);
      response = Call(wallet, &read);
      std::string value;
      if (response && dbus::MessageReader(response.get()).PopString(&value) &&
          !value.empty()) {
        secret = std::move(value);
      }
    }
    dbus::MethodCall close(kKWalletInterface, "close");
    dbus::MessageWriter close_writer(&close);
    close_writer.AppendInt32(handle);
    close_writer.AppendBool(false);
    close_writer.AppendString(kKWalletAppId);
    Call(wallet, &close);
    return secret;
  }
  return std::nullopt;
}

}  // namespace

std::optional<std::string> ReadKeyringSecret(std::string_view application,
                                             std::string_view kwallet_folder,
                                             std::string_view key) {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SESSION;
  options.connection_type = dbus::Bus::PRIVATE;
  auto bus = base::MakeRefCounted<dbus::Bus>(std::move(options));
  std::optional<std::string> secret;
  if (!application.empty()) {
    secret = ReadSecretService(bus.get(), application);
  }
  if (!secret && !kwallet_folder.empty() && !key.empty()) {
    secret = ReadKWallet(bus.get(), kwallet_folder, key);
  }
  bus->ShutdownAndBlock();
  return secret;
}

std::string AppIconDataUrl(std::string_view bundle_id) {
  return LoadedAppIconDataUrl(bundle_id);
}

}  // namespace koan::importer
