// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// A Space's profile's partition in its service-worker notifications' ids
// (#530, koan_partition_domain.h).

#include "koan/browser/containers/koan_partition_domain.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace koan::containers {
namespace {

constexpr char kDomain[] = "koanusersabcdefghijklmnopqrstuvwxyz";

// Upstream's forms (content's NotificationIdGenerator::
// GenerateForPersistentNotification(): "p", "b" if shown by the browser, '#',
// the origin, '#', then "0" and the number or "1" and the tag). If upstream
// changes them, KoanSpaceNotificationsBrowserTest.ServiceWorkerNotification*
// fail too: the displayed id must name the profile.
constexpr char kNumbered[] = "p#https://mail.example.com#010042";
constexpr char kTagged[] = "p#https://mail.example.com#1inbox";
constexpr char kByBrowser[] = "pb#https://mail.example.com#010042";

TEST(PartitionNotificationIdTest, NamesAUserContainersPartition) {
  EXPECT_EQ(std::string("p") + kDomain + "#https://mail.example.com#010042",
            TagPersistentNotificationId(kNumbered, kDomain));
  EXPECT_EQ(std::string("pb") + kDomain + "#https://mail.example.com#010042",
            TagPersistentNotificationId(kByBrowser, kDomain));
  for (const char* id : {kNumbered, kTagged, kByBrowser}) {
    EXPECT_EQ(kDomain, PartitionDomainOfNotificationId(
                           TagPersistentNotificationId(id, kDomain)))
        << id;
  }
}

TEST(PartitionNotificationIdTest, UpstreamsIdsStayUpstreams) {
  // The default partition (empty domain), an agent's, an extension's.
  for (const char* domain :
       {"", "koanagentsabcdefgh", "abcdefghijklmnopabcdefghijklmnop"}) {
    EXPECT_EQ(kNumbered, TagPersistentNotificationId(kNumbered, domain))
        << domain;
  }
  for (const char* id : {kNumbered, kTagged, kByBrowser}) {
    EXPECT_EQ("", PartitionDomainOfNotificationId(id)) << id;
  }
  // A page's own notification ("n#<origin>#<token>") is never tagged.
  EXPECT_EQ(
      "n#https://mail.example.com#token",
      TagPersistentNotificationId("n#https://mail.example.com#token", kDomain));
}

TEST(PartitionNotificationIdTest, SitesCantForgeIt) {
  // The tag is the site's and comes after the origin.
  const std::string tag = std::string("p#https://evil.example#1") + kDomain;
  EXPECT_EQ("", PartitionDomainOfNotificationId(tag));
  EXPECT_EQ("", PartitionDomainOfNotificationId(
                    std::string("p#https://evil.example#1#") + kDomain + "#"));
}

TEST(PartitionNotificationIdTest, OnlyWellFormedDomains) {
  EXPECT_TRUE(IsWellFormedUserPartitionDomain(kDomain));
  EXPECT_TRUE(IsWellFormedUserPartitionDomain("koanuserst"));
  EXPECT_FALSE(IsWellFormedUserPartitionDomain("koanusers"));
  EXPECT_FALSE(IsWellFormedUserPartitionDomain("koanuserS1"));
  EXPECT_FALSE(IsWellFormedUserPartitionDomain("koanuser../x"));
  EXPECT_FALSE(IsWellFormedUserPartitionDomain(std::string("koanuser") +
                                               std::string(41, 's')));
  EXPECT_FALSE(IsWellFormedUserPartitionDomain("koanagentsabc"));
  EXPECT_EQ("",
            PartitionDomainOfNotificationId("pkoanuserS1#https://a.test#0"));
  EXPECT_EQ("", PartitionDomainOfNotificationId("p"));
  EXPECT_EQ("", PartitionDomainOfNotificationId(""));
  // Not upstream's form: left alone.
  EXPECT_EQ("px#https://a.test#0",
            TagPersistentNotificationId("px#https://a.test#0", kDomain));
}

}  // namespace
}  // namespace koan::containers
