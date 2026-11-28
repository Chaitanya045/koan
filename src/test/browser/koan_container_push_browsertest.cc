// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// A container's service workers and the profile's push subscriptions (#596,
// .agent/notes/upstream/space-containers.md, "Push and a container's service
// workers"). A subscription is keyed by origin and service-worker
// registration id, and registration ids are per partition, so a worker in a
// Space's profile or an agent container can have the id of the default
// partition's subscribed worker. Unregistering it, or clearing the
// container's data, must leave the default partition's subscription alone.

#include <cstdint>
#include <optional>
#include <string>

#include "base/containers/flat_set.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/push_messaging/push_messaging_app_identifier.h"
#include "chrome/browser/push_messaging/push_messaging_service_factory.h"
#include "chrome/browser/push_messaging/push_messaging_service_impl.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/push_messaging/app_identifier.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/service_worker_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/test/browser_test.h"
#include "koan/browser/containers/koan_container_partition.h"
#include "koan/browser/containers/koan_user_containers.h"
#include "koan/browser/koan_prefs.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/blink/public/common/service_worker/service_worker_status_code.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "third_party/blink/public/mojom/push_messaging/push_messaging_status.mojom.h"
#include "third_party/blink/public/mojom/service_worker/service_worker_registration_options.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace koan {

namespace {

// Chromium's push test worker (chrome/test/data/push_messaging).
constexpr char kWorker[] = "/push_messaging/service_worker.js";
constexpr char kScope[] = "/push_messaging/";

constexpr char kUnregistrationReason[] = "PushMessaging.UnregistrationReason";

// The registrations `context` stores and deletes.
class RegistrationWatcher
    : public content::ServiceWorkerContextObserverSynchronous {
 public:
  explicit RegistrationWatcher(content::ServiceWorkerContext* context) {
    observation_.Observe(context);
  }

  // The id of the next registration stored.
  std::optional<int64_t> WaitForStored() {
    if (!base::test::RunUntil([&] { return stored_.has_value(); })) {
      return std::nullopt;
    }
    return stored_;
  }

  // Whether registration `id` was deleted.
  bool deleted(int64_t id) const { return deleted_.contains(id); }

  // content::ServiceWorkerContextObserverSynchronous:
  void OnRegistrationStoredSync(int64_t registration_id,
                                const GURL& scope) override {
    stored_ = registration_id;
  }
  void OnRegistrationDeletedSync(int64_t registration_id,
                                 const GURL& scope) override {
    deleted_.insert(registration_id);
  }

 private:
  std::optional<int64_t> stored_;
  base::flat_set<int64_t> deleted_;
  base::ScopedObservation<content::ServiceWorkerContext,
                          content::ServiceWorkerContextObserverSynchronous>
      observation_{this};
};

class KoanContainerPushBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
    push_service()->SetServiceWorkerUnregisteredCallbackForTesting(
        base::BindRepeating([](int* count) { ++*count; },
                            &default_unregistered_));
  }

 protected:
  Profile* profile() { return browser()->GetProfile(); }
  PushMessagingServiceImpl* push_service() {
    return PushMessagingServiceFactory::GetForProfile(profile());
  }

  content::StoragePartition* DefaultPartition() {
    return profile()->GetDefaultStoragePartition();
  }

  // The partition of a new profile (New Profile...), as a Space with it uses.
  content::StoragePartition* SpaceProfilePartition() {
    ScopedListPrefUpdate update(profile()->GetPrefs(), prefs::kUserContainers);
    std::optional<containers::UserContainer> container =
        containers::AddUserContainer(update.Get(), u"Work Profile");
    EXPECT_TRUE(container);
    return container
               ? profile()->GetStoragePartition(
                     containers::PartitionConfigFor(profile(), container->id))
               : nullptr;
  }

  // A throwaway agent container's partition, as a task gets one.
  content::StoragePartition* AgentPartition() {
    return profile()->GetStoragePartition(containers::PartitionConfigFor(
        profile(), {.kind = containers::ContainerKind::kAgent,
                    .id = containers::NewContainerId(/*persistent=*/false)}));
  }

  // Registers the push test worker of `host` in `partition`, for `scope_path`;
  // its registration id.
  std::optional<int64_t> Register(content::StoragePartition* partition,
                                  const std::string& host,
                                  const std::string& scope_path = kScope) {
    content::ServiceWorkerContext* context =
        partition->GetServiceWorkerContext();
    RegistrationWatcher watcher(context);
    const GURL scope = embedded_test_server()->GetURL(host, scope_path);
    blink::mojom::ServiceWorkerRegistrationOptions options(
        scope, blink::mojom::ScriptType::kClassic,
        blink::mojom::ServiceWorkerUpdateViaCache::kImports);
    base::RunLoop run_loop;
    blink::ServiceWorkerStatusCode status =
        blink::ServiceWorkerStatusCode::kErrorFailed;
    context->RegisterServiceWorker(
        embedded_test_server()->GetURL(host, kWorker),
        blink::StorageKey::CreateFirstParty(url::Origin::Create(scope)),
        options, content::GlobalRenderFrameHostId(),
        base::BindOnce(
            [](blink::ServiceWorkerStatusCode* out, base::OnceClosure done,
               blink::ServiceWorkerStatusCode result) {
              *out = result;
              std::move(done).Run();
            },
            &status, run_loop.QuitClosure()));
    run_loop.Run();
    EXPECT_EQ(blink::ServiceWorkerStatusCode::kOk, status);
    return watcher.WaitForStored();
  }

  // Registers the site's worker in `partition` with registration id `id`:
  // the id of the default partition's subscribed worker. Registration ids are
  // a counter of each partition's own, from 0, and the default partition can
  // be ahead (a built-in extension's worker), so other workers of the site
  // (other scopes) take the ids before it. Nullopt, or another id, if ids
  // are not a counter per partition any more.
  std::optional<int64_t> RegisterWithId(content::StoragePartition* partition,
                                        int64_t id) {
    for (int pad = 0; pad < id; ++pad) {
      const std::optional<int64_t> got = Register(
          partition, kSite,
          std::string(kScope) + "pad" + base::NumberToString(pad) + "/");
      if (!got || *got >= id - 1) {
        break;
      }
    }
    return Register(partition, kSite);
  }

  // Unregisters `host`'s worker in `partition`, as the page's
  // registration.unregister() does.
  void Unregister(content::StoragePartition* partition,
                  const std::string& host) {
    const GURL scope = embedded_test_server()->GetURL(host, kScope);
    base::RunLoop run_loop;
    partition->GetServiceWorkerContext()->UnregisterServiceWorkerImmediately(
        scope, blink::StorageKey::CreateFirstParty(url::Origin::Create(scope)),
        base::BindOnce(
            [](base::OnceClosure done, blink::ServiceWorkerStatusCode result) {
              EXPECT_EQ(blink::ServiceWorkerStatusCode::kOk, result);
              std::move(done).Run();
            },
            run_loop.QuitClosure()));
    run_loop.Run();
  }

  // Clears everything `partition` holds, as an agent's throwaway container
  // is cleared when its task ends (ReleaseContainer()).
  void ClearData(content::StoragePartition* partition) {
    base::RunLoop run_loop;
    partition->ClearData(content::StoragePartition::REMOVE_DATA_MASK_ALL,
                         blink::StorageKey(), base::Time(), base::Time::Max(),
                         run_loop.QuitClosure());
    run_loop.Run();
  }

  // Stands for the default partition's push subscription of `host`'s worker
  // `id`: what subscribe() stores in the profile's prefs (a real one needs
  // the push service's servers).
  void Subscribe(const std::string& host, int64_t id) {
    PushMessagingAppIdentifier::PersistToPrefs(
        push_messaging::AppIdentifier::Generate(Origin(host), id), profile());
  }

  bool IsSubscribed(const std::string& host, int64_t id) {
    return !PushMessagingAppIdentifier::FindByServiceWorker(profile(),
                                                            Origin(host), id)
                .is_null();
  }

  GURL Origin(const std::string& host) {
    return embedded_test_server()->GetURL(host, "/").DeprecatedGetOriginAsURL();
  }

  // Lets every service-worker event posted so far reach the push service:
  // unregisters a default worker that has no subscription, which the push
  // service answers at once. The events go to the UI thread in order.
  void FlushEvents(const std::string& control_host) {
    const int before = default_unregistered_;
    Unregister(DefaultPartition(), control_host);
    EXPECT_TRUE(
        base::test::RunUntil([&] { return default_unregistered_ > before; }));
  }

  // The default partition's subscribed worker of 127.0.0.1, and a second
  // default worker (localhost) for FlushEvents(). Its registration id.
  std::optional<int64_t> SetUpDefaultSubscription() {
    const std::optional<int64_t> id = Register(DefaultPartition(), kSite);
    if (!id || !Register(DefaultPartition(), kControl)) {
      return std::nullopt;
    }
    Subscribe(kSite, *id);
    return id;
  }

  // The default partition's subscription still unsubscribes as upstream has
  // it when its own worker goes.
  void ExpectDefaultStillUnsubscribes(int64_t id) {
    base::HistogramTester histograms;
    Unregister(DefaultPartition(), kSite);
    EXPECT_TRUE(base::test::RunUntil([&] { return !IsSubscribed(kSite, id); }));
    histograms.ExpectUniqueSample(
        kUnregistrationReason,
        blink::mojom::PushUnregistrationReason::SERVICE_WORKER_UNREGISTERED, 1);
  }

  // Both are potentially trustworthy, so both can have service workers.
  static constexpr char kSite[] = "127.0.0.1";
  static constexpr char kControl[] = "localhost";

  int default_unregistered_ = 0;
};

}  // namespace

// A Space's profile: the same site's worker, with the same registration id,
// unregistered there.
IN_PROC_BROWSER_TEST_F(KoanContainerPushBrowserTest,
                       UnregisterInSpaceProfileKeepsDefaultSubscription) {
  const std::optional<int64_t> id = SetUpDefaultSubscription();
  ASSERT_TRUE(id);
  content::StoragePartition* space = SpaceProfilePartition();
  ASSERT_TRUE(space);
  // The collision: registration ids are each partition's own. If upstream
  // makes them unique per profile, this fails, and the hook may go
  // (docs/REBASE.md).
  ASSERT_EQ(id, RegisterWithId(space, *id));

  base::HistogramTester histograms;
  RegistrationWatcher watcher(space->GetServiceWorkerContext());
  Unregister(space, kSite);
  ASSERT_TRUE(watcher.deleted(*id));
  FlushEvents(kControl);
  EXPECT_TRUE(IsSubscribed(kSite, *id));
  histograms.ExpectTotalCount(kUnregistrationReason, 0);

  ExpectDefaultStillUnsubscribes(*id);
}

// An agent container: the same, its data cleared when its task ends.
IN_PROC_BROWSER_TEST_F(KoanContainerPushBrowserTest,
                       ClearingAgentContainerKeepsDefaultSubscription) {
  const std::optional<int64_t> id = SetUpDefaultSubscription();
  ASSERT_TRUE(id);
  content::StoragePartition* agent = AgentPartition();
  ASSERT_TRUE(agent);
  ASSERT_EQ(id, RegisterWithId(agent, *id));

  base::HistogramTester histograms;
  RegistrationWatcher watcher(agent->GetServiceWorkerContext());
  ClearData(agent);
  ASSERT_TRUE(watcher.deleted(*id));
  FlushEvents(kControl);
  EXPECT_TRUE(IsSubscribed(kSite, *id));
  histograms.ExpectTotalCount(kUnregistrationReason, 0);

  ExpectDefaultStillUnsubscribes(*id);
}

}  // namespace koan
