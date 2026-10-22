// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenUBActionsProvider.sys.mjs (fuzzy scoring)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

#include "koan/browser/ui/views/koan_command_palette.h"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "components/omnibox/browser/actions/omnibox_action.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_provider.h"
#include "components/omnibox/browser/autocomplete_provider_client.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "components/omnibox/browser/omnibox_event_global_tracker.h"
#include "components/omnibox/browser/omnibox_log.h"
#include "components/omnibox/browser/omnibox_popup_selection.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "koan/browser/koan_features.h"
#include "koan/browser/koan_prefs.h"
#include "koan/browser/omnibox/koan_omnibox_hooks.h"
#include "koan/browser/ui/views/koan_browser_view_controller.h"
#include "koan/browser/ui/views/koan_commands.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "third_party/metrics_proto/omnibox_input_type.pb.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/ui_base_features.h"
#include "ui/base/webui/web_ui_util.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "url/gurl.h"

namespace koan {

namespace {

// Zen (ZenUBActionsProvider): typed text suggests actions scoring above 92,
// at most five in Zen and three here, so they don't crowd history and search
// suggestions out of Chromium's eight rows; ">" lists every action scoring
// above 30.
constexpr size_t kMaxSuggestedActions = 3;
constexpr int kMinimumScore = 92;
constexpr int kMinimumPrefixedScore = 30;
constexpr size_t kMinimumQueryLength = 3;
constexpr size_t kMaxQueryLength = 255;
// Exact and prefix matches outrank any fuzzy match.
constexpr int kExactMatchScore = 1'000'000;
constexpr int kPrefixMatchScore = 100'000;
// With ">", Enter runs the first action once this much is typed, as in Zen.
constexpr size_t kMinimumPrefixedQueryForDefault = 2;
// The learner's bounds (ZenUBResultsLearner), and how many actions it
// remembers (passed-over Spaces that are long gone, for example, fall out).
constexpr int kMaxUsage = 5;
constexpr int kMinUsage = -5;
constexpr size_t kMaxUsageEntries = 64;

// Relevances. Typed actions stay below URL inline autocompletion and history
// navigation (1400 and up), so typing part of a site's address never runs
// one.
// - With ">" there is nothing else to pick, so actions come first.
constexpr int kActionsModeRelevance = 1600;
// - A typed action may be the default match only for its exact label, once
//   the user has run it from here, and then beats only
//   search-what-you-typed (1300).
constexpr int kTypedDefaultRelevance = 1301;
// - Other typed actions rank with search suggestions, lower each time the
//   user passed them over (Zen moves those down the list).
constexpr int kTypedRelevance = 1250;
constexpr int kPassedOverPenalty = 100;

// The popup's icon size (--cr-searchbox-results-search-icon-size).
constexpr int kIconSize = 16;

// Actions are matches of type kPedal (Chrome's own action suggestions) with
// a unique URL under this prefix, which keeps them apart when the result is
// de-duplicated by URL and identifies the command. The URL is never opened:
// the match's takeover action runs instead.
constexpr char kCommandUrlPrefix[] = "chrome://koan-command/";

// Firefox's UrlUtils.REGEXP_LIKE_PROTOCOL, /^[A-Z+.-]+:/i: text Zen keeps
// for URLs.
bool LooksLikeProtocol(std::u16string_view text) {
  size_t i = 0;
  while (i < text.size() && (base::IsAsciiAlpha(text[i]) || text[i] == '+' ||
                             text[i] == '.' || text[i] == '-')) {
    ++i;
  }
  return i > 0 && i < text.size() && text[i] == ':';
}

// The window being typed in: the most recently active one of `prefs`'s
// profile whose URL bar has focus, else the most recently active one.
KoanBrowserViewController* ControllerFor(const PrefService* prefs) {
  KoanBrowserViewController* focused = nullptr;
  KoanBrowserViewController* active = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* browser) {
        BrowserView* browser_view =
            BrowserView::GetBrowserViewForBrowser(browser);
        KoanBrowserViewController* controller =
            browser_view ? KoanBrowserViewController::From(browser_view)
                         : nullptr;
        if (!controller || browser_view->GetProfile()->GetPrefs() != prefs) {
          return true;
        }
        if (!active) {
          active = controller;
        }
        LocationBarView* location_bar = browser_view->GetLocationBarView();
        if (location_bar && location_bar->omnibox_view() &&
            location_bar->omnibox_view()->HasFocus()) {
          focused = controller;
          return false;
        }
        return true;
      },
      BrowserCollection::Order::kActivation);
  return focused ? focused : active;
}

// What a screen reader says for the row: Chromium announces only the action's
// accessibility hint for kPedal matches.
std::u16string AccessibleName(const KoanCommand& command) {
  std::u16string name = command.label;
  if (!command.target.empty()) {
    name += u" " + command.target;
  }
  if (!command.shortcut.empty()) {
    name += u", " + command.shortcut;
  }
  return name;
}

// How well `query` matches `command`: by its target (a Space's name) if it
// has one, else its label. A Space's name may start with its emoji icon,
// which the user doesn't type.
int Score(const KoanCommand& command, std::u16string_view query) {
  const std::u16string text = base::i18n::ToLower(
      command.target.empty() ? command.label : command.target);
  int score = KoanCommandPalette::FuzzyScore(text, query);
  const size_t space = text.find(u' ');
  if (space != std::u16string::npos && !text.empty() &&
      !base::IsAsciiAlphaNumeric(text[0])) {
    score = std::max(score,
                     KoanCommandPalette::FuzzyScore(
                         std::u16string_view(text).substr(space + 1), query));
  }
  return score;
}

int GetUsage(const PrefService* prefs, const std::string& id) {
  return prefs->GetDict(prefs::kCommandPaletteUsage).FindInt(id).value_or(0);
}

void RunCommand(base::WeakPtr<KoanBrowserViewController> controller,
                const KoanCommand::RunCallback& run) {
  if (!controller) {
    return;
  }
  // Like Zen, the page takes focus back first, which also closes the
  // floating URL bar; an action that needs focus (Find in Page) takes it.
  if (content::WebContents* contents =
          controller->browser_view()->GetActiveWebContents()) {
    contents->Focus();
  }
  run.Run(*controller);
}

// Runs a KoanCommand when its suggestion is opened. The command's own icon
// is the match's `icon_url`; the vector icon is the search icon, as in Zen's
// URL bar in the Actions mode, and one of the few the WebUI popup has an SVG
// for (anything else hits a NOTREACHED there).
class KoanCommandAction : public OmniboxAction {
 public:
  KoanCommandAction(const KoanCommand& command,
                    base::WeakPtr<KoanBrowserViewController> controller)
      : OmniboxAction(LabelStrings(command.label,
                                   command.label,
                                   std::u16string(),
                                   AccessibleName(command)),
                      GURL()),
        run_(command.run),
        controller_(std::move(controller)) {}

  // OmniboxAction:
  void Execute(ExecutionContext& context) const override {
    // Later: the omnibox is still handling the key press or click.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&RunCommand, controller_, run_));
  }
#if defined(SUPPORT_PEDALS_VECTOR_ICONS)
  const gfx::VectorIcon& GetVectorIcon() const override {
    return ::features::IsRoundedIconsEnabled()
               ? vector_icons::kSearchIcon
               : vector_icons::kSearchChromeRefreshOldIcon;
  }
#endif

 private:
  ~KoanCommandAction() override = default;

  const KoanCommand::RunCallback run_;
  const base::WeakPtr<KoanBrowserViewController> controller_;
};

// Zen's ZenUrlbarProviderGlobalActions: suggests the window's commands.
class KoanCommandProvider : public AutocompleteProvider {
 public:
  // There is no provider type for Koan: kBuiltin, like Chrome's chrome://
  // page suggestions, runs where the omnibox does and shares their metrics.
  explicit KoanCommandProvider(AutocompleteProviderClient* client)
      : AutocompleteProvider(AutocompleteProvider::Type::kBuiltin),
        client_(client) {
    // The learner: which of this provider's suggestions the user opened.
    url_opened_subscription_ =
        OmniboxEventGlobalTracker::GetInstance()->RegisterCallback(
            base::BindRepeating(&KoanCommandProvider::OnURLOpened,
                                base::Unretained(this)));
  }
  KoanCommandProvider(const KoanCommandProvider&) = delete;
  KoanCommandProvider& operator=(const KoanCommandProvider&) = delete;

  // AutocompleteProvider:
  void Start(const AutocompleteInput& input, bool minimal_changes) override;

 private:
  ~KoanCommandProvider() override = default;

  void OnURLOpened(OmniboxLog* log);

  // `icon` drawn in `color`, as a PNG data URL: a match's `icon_url`, which
  // the WebUI popup shows as an image, and Chrome's Views popup and location
  // bar after fetching it.
  const GURL& GetIconUrl(const gfx::VectorIcon& icon, SkColor color);

  const raw_ptr<AutocompleteProviderClient> client_;
  base::CallbackListSubscription url_opened_subscription_;
  std::map<std::pair<const gfx::VectorIcon*, SkColor>, GURL> icon_urls_;
};

void KoanCommandProvider::Start(const AutocompleteInput& input,
                                bool minimal_changes) {
  matches_.clear();
  // Not in a search engine's keyword mode: the text is for the engine.
  if (!features::IsCommandPaletteEnabled() || input.IsZeroSuggest() ||
      input.omit_asynchronous_matches() || input.in_keyword_mode() ||
      input.current_page_classification() ==
          metrics::OmniboxEventProto::NTP_REALBOX) {
    return;
  }
  PrefService* prefs = client_->GetPrefs();
  std::u16string_view text = input.text();
  const bool prefixed = text.starts_with(kActionsModePrefix);
  if (prefixed) {
    text.remove_prefix(1);
  } else if (!prefs->GetBoolean(prefs::kCommandPaletteSuggestions) ||
             text.size() < kMinimumQueryLength ||
             text.size() >= kMaxQueryLength || LooksLikeProtocol(text)) {
    return;
  }
  const std::u16string query = base::i18n::ToLower(
      base::TrimWhitespace(text, base::TrimPositions::TRIM_ALL));
  if (query.empty() && !prefixed) {
    return;
  }
  KoanBrowserViewController* controller = ControllerFor(prefs);
  if (!controller) {
    return;
  }

  struct Candidate {
    KoanCommand command;
    int score;
    int usage;
  };
  std::vector<Candidate> candidates;
  for (KoanCommand& command :
       controller->command_palette()->registry().GetAvailableCommands()) {
    const int score =
        query.empty() ? kMinimumPrefixedScore + 1 : Score(command, query);
    if (score > (prefixed ? kMinimumPrefixedScore : kMinimumScore)) {
      const int usage = GetUsage(prefs, command.id);
      candidates.push_back({std::move(command), score, usage});
    }
  }
  std::ranges::stable_sort(candidates, std::ranges::greater(),
                           &Candidate::score);
  // Zen lists everything with ">", so nothing is out of reach.
  if (!prefixed && candidates.size() > kMaxSuggestedActions) {
    candidates.resize(kMaxSuggestedActions);
  }
  std::ranges::stable_sort(candidates, std::ranges::greater(),
                           &Candidate::usage);
  const ui::ColorProvider* color_provider =
      controller->browser_view()->GetColorProvider();

  for (size_t i = 0; i < candidates.size(); ++i) {
    const KoanCommand& command = candidates[i].command;
    const int usage = candidates[i].usage;
    // Enter runs the first action with ">" and two characters (Zen's
    // heuristic result), and a typed action the user ran from here before
    // when the text is its whole label (Zen: any match).
    const bool is_default =
        i == 0 && !input.parts().scheme.is_nonempty() &&
        (prefixed ? query.size() >= kMinimumPrefixedQueryForDefault
                  : usage > 0 && candidates[i].score == kExactMatchScore &&
                        input.type() != metrics::OmniboxInputType::URL);
    int relevance = kActionsModeRelevance;
    if (!prefixed) {
      relevance = is_default ? kTypedDefaultRelevance
                             : kTypedRelevance -
                                   kPassedOverPenalty * std::max(0, -usage);
    }
    AutocompleteMatch match(this, relevance - static_cast<int>(i),
                            /*deletable=*/false,
                            omnibox::AutocompleteMatchType::kPedal);
    match.allowed_to_be_default_match = is_default;
    // Listed right after the default match, above search suggestions, like
    // Zen's (which go at index 1); Chrome sorts boosted shortcuts there. Not
    // once the user has passed the action over.
    match.shortcut_boosted = prefixed || usage >= 0;
    match.destination_url = GURL(kCommandUrlPrefix + command.id);
    match.transition = ui::PAGE_TRANSITION_GENERATED;
    // Selecting the action keeps what the user typed in the box.
    match.fill_into_edit = input.text();
    // Zen shows the Space's name as a tag beside "Focus on", and the
    // shortcut at the end of the row. A match with a description shows it
    // first (AutocompleteController swaps them for non-search matches), so
    // the label goes there when there's something to show after it.
    const std::u16string& extra =
        command.target.empty() ? command.shortcut : command.target;
    if (extra.empty()) {
      match.contents = command.label;
      match.contents_class = {{0, ACMatchClassification::NONE}};
    } else {
      match.description = command.label;
      match.description_class = {{0, ACMatchClassification::NONE}};
      match.contents = extra;
      match.contents_class = {{0, ACMatchClassification::DIM}};
    }
    if (command.icon && color_provider) {
      match.icon_url = GetIconUrl(
          *command.icon, color_provider->GetColor(kColorOmniboxResultsIcon));
    }
    match.takeover_action = base::MakeRefCounted<KoanCommandAction>(
        command, controller->GetWeakPtr());
    matches_.push_back(std::move(match));
  }
}

const GURL& KoanCommandProvider::GetIconUrl(const gfx::VectorIcon& icon,
                                            SkColor color) {
  GURL& url = icon_urls_[{&icon, color}];
  if (url.is_empty()) {
    // Crisp on 2x screens; the popup sizes it.
    const gfx::ImageSkia image = gfx::CreateVectorIcon(icon, kIconSize, color);
    url = GURL(
        webui::GetBitmapDataUrl(image.GetRepresentation(2.0f).GetBitmap()));
  }
  return url;
}

void KoanCommandProvider::OnURLOpened(OmniboxLog* log) {
  const AutocompleteResult& result = *log->result;
  std::optional<std::string> used;
  std::vector<std::string> seen;
  for (size_t i = 0; i < result.size(); ++i) {
    const AutocompleteMatch& match = result.match_at(i);
    if (match.provider != this) {
      continue;
    }
    std::string id = match.destination_url.spec().substr(
        std::char_traits<char>::length(kCommandUrlPrefix));
    if (i == log->selection.line &&
        log->selection.state == OmniboxPopupSelection::LineState::kNormal) {
      used = std::move(id);
    } else {
      seen.push_back(std::move(id));
    }
  }
  // Like Zen, the full ">" list doesn't count as passing actions over.
  std::u16string_view text = log->text;
  if (text.starts_with(kActionsModePrefix) && text.size() < 3) {
    seen.clear();
  }
  if (!used && seen.empty()) {
    return;
  }
  ScopedDictPrefUpdate update(client_->GetPrefs(), prefs::kCommandPaletteUsage);
  if (used) {
    update->Set(*used,
                std::min(update->FindInt(*used).value_or(0) + 1, kMaxUsage));
  }
  for (const std::string& id : seen) {
    const int usage = update->FindInt(id).value_or(0);
    const int new_usage = usage == 0 ? -1 : std::max(usage - 1, kMinUsage);
    if (new_usage == 0) {
      update->Remove(id);
    } else {
      update->Set(id, new_usage);
    }
  }
  // Forget passed-over actions beyond the limit (ids sort alphabetically).
  std::vector<std::string> forget;
  for (const auto [id, usage] : *update) {
    if (update->size() - forget.size() <= kMaxUsageEntries) {
      break;
    }
    if (usage.GetIfInt().value_or(0) < 0) {
      forget.push_back(id);
    }
  }
  for (const std::string& id : forget) {
    update->Remove(id);
  }
}

}  // namespace

KoanCommandPalette::KoanCommandPalette(KoanBrowserViewController* controller)
    : controller_(controller) {
  AddGlobalCommands(registry_, *controller);
  AddWorkspaceCommands(registry_, *controller);
}

KoanCommandPalette::~KoanCommandPalette() {
  OnBrowserViewDestroying();
}

void KoanCommandPalette::OnAddedToWidget() {
  LocationBarView* location_bar =
      controller_->browser_view()->GetLocationBarView();
  if (omnibox_ || !location_bar || !location_bar->omnibox_view()) {
    return;
  }
  omnibox_ = location_bar->omnibox_view();
  omnibox_->AddPreTargetHandler(this);
}

void KoanCommandPalette::OnBrowserViewDestroying() {
  if (omnibox_) {
    omnibox_->RemovePreTargetHandler(this);
    omnibox_ = nullptr;
  }
}

void KoanCommandPalette::OnKeyEvent(ui::KeyEvent* event) {
  // Zen: Tab in an empty URL bar switches to the Actions mode.
  if (event->type() != ui::EventType::kKeyPressed ||
      event->key_code() != ui::VKEY_TAB ||
      (event->flags() &
       (ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN)) ||
      !features::IsCommandPaletteEnabled()) {
    return;
  }
  // A second Tab leaves the Actions mode, as in Zen.
  const std::u16string text = omnibox_->GetText();
  const std::u16string actions_mode(1, kActionsModePrefix);
  if (text == actions_mode) {
    omnibox_->SetUserText(std::u16string());
  } else if (text.empty()) {
    omnibox_->SetUserText(actions_mode);
    omnibox_->GetViewAccessibility().AnnouncePolitely(u"Actions");
  } else {
    return;
  }
  event->SetHandled();
}

// static
int KoanCommandPalette::FuzzyScore(std::u16string_view target,
                                   std::u16string_view query) {
  // A VS Code-style score, as in Zen (ZenUBActionsProvider).
  if (target.empty() || query.empty() || query.size() > target.size()) {
    return 0;
  }
  if (target == query) {
    return kExactMatchScore;
  }
  if (target.starts_with(query)) {
    return kPrefixMatchScore + static_cast<int>(query.size());
  }
  int score = 0;
  size_t matched = 0;
  // -1 before the first match; Zen's run bonus then also counts a match at
  // index 0 as continuing a run.
  int last_match = -1;
  int run = 0;
  for (int i = 0; i < static_cast<int>(target.size()) && matched < query.size();
       ++i) {
    if (target[i] != query[matched]) {
      continue;
    }
    int bonus = 10;
    if (i == 0 || target[i - 1] == u' ' || target[i - 1] == u'-' ||
        target[i - 1] == u'_') {
      bonus += 15;  // Start of a word.
    }
    if (last_match == i - 1) {
      ++run;
      bonus += 20 * run;
    } else {
      run = 0;
    }
    if (last_match != -1) {
      bonus -= std::min(i - last_match - 1, 10);  // Gap since the last match.
    }
    score += bonus;
    last_match = i;
    ++matched;
  }
  return matched == query.size() ? score : 0;
}

void AddAutocompleteProviders(
    AutocompleteController* controller,
    int provider_types,
    std::vector<scoped_refptr<AutocompleteProvider>>& providers) {
  // Wherever Chrome's built-in suggestions (chrome:// pages) are, which is
  // every omnibox-like box; KoanCommandProvider then picks the URL bar.
  if (provider_types & static_cast<int>(AutocompleteProvider::Type::kBuiltin)) {
    providers.push_back(base::MakeRefCounted<KoanCommandProvider>(
        controller->autocomplete_provider_client()));
  }
}

}  // namespace koan
