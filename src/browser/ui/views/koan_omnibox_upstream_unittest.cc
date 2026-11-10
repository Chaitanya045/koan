// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// The two behaviours of Chromium's AutocompleteController that
// KoanUrlBarController's empty-list request (OnOmniboxChanged() /
// RetryEmptyList()) is built on. If a roll changes either, this fails and the
// request must be looked at again (docs/REBASE.md, "Omnibox lockdown").

#include <memory>
#include <vector>

#include "base/memory/scoped_refptr.h"
#include "base/test/task_environment.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "components/omnibox/browser/autocomplete_controller_config.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_provider.h"
#include "components/omnibox/browser/fake_autocomplete_provider_client.h"
#include "components/omnibox/browser/test_scheme_classifier.h"
#include "koan/browser/omnibox/koan_omnibox_hooks.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "third_party/metrics_proto/omnibox_focus_type.pb.h"

// The patched AutocompleteController constructor calls this (the command
// palette lives in the views target, which this test does not link): no
// provider.
namespace koan {
void AddAutocompleteProviders(
    AutocompleteController* controller,
    int provider_types,
    std::vector<scoped_refptr<AutocompleteProvider>>& providers) {}
}  // namespace koan

namespace koan {
namespace {

class Recorder : public AutocompleteController::Observer {
 public:
  void OnResultChanged(AutocompleteController* controller,
                       bool default_match_changed) override {
    ++changes;
    last_default_match_changed = default_match_changed;
  }
  int changes = 0;
  bool last_default_match_changed = false;
};

TEST(KoanUpstreamAssumptionsTest, EveryRequestsFirstPassReportsAChange) {
  base::test::TaskEnvironment task_environment;
  AutocompleteControllerConfig config;
  config.provider_types = 0;
  AutocompleteController controller(
      std::make_unique<FakeAutocompleteProviderClient>(), config);
  Recorder recorder;
  controller.AddObserver(&recorder);

  TestSchemeClassifier classifier;
  AutocompleteInput input(u"", metrics::OmniboxEventProto::OTHER, classifier);
  input.set_focus_type(metrics::OmniboxFocusType::INTERACTION_FOCUS);

  // Twice, with an empty result both times: the second request reports a
  // change as well (default_match_changed is forced on a sync pass), which is
  // why asking again from the change handler must be limited to the change
  // from text to none.
  for (int i = 0; i < 2; ++i) {
    recorder.changes = 0;
    recorder.last_default_match_changed = false;
    controller.Start(input);
    EXPECT_GE(recorder.changes, 1) << "request " << i;
    EXPECT_TRUE(recorder.last_default_match_changed) << "request " << i;
    // And an empty result is done at once: the retry asks only when done().
    EXPECT_TRUE(controller.done()) << "request " << i;
    EXPECT_TRUE(controller.result().empty()) << "request " << i;
  }
  controller.RemoveObserver(&recorder);
}

}  // namespace
}  // namespace koan
