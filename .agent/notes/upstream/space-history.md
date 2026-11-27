# History per Space profile (#474): design and upstream audit

Date: 2026-11-27. Chromium 158 (pristine `git show HEAD:`), Zen 1.24b, brave-core (read via `gh api`, read only).
It builds on `space-containers.md`, where a Space's profile is a saved user container (its own
StoragePartition), and on `omnibox-lockdown.md` for the URL bar. Zen and Brave are MPL-2.0. They were read
for behaviour only, and none of their code is in Koan.

## What others do

- **Zen / Firefox.** Containers share one history (`places.sqlite` has no `userContextId` column). Only
  "Switch to Tab" knows containers: `browser.urlbar.switchTabs.searchAllContainers` lists every container's
  tabs. Zen 1.24b adds nothing for history. Its users ask for it: zen-browser/desktop#2337 and #818.
- **Brave.** `components/containers` (brave-core) has no history part. History is shared. Container-aware
  history is on the "Containers part 2" list (brave/brave-browser#55380).
- **Arc.** A Space tied to a profile is a separate browser profile, so history is apart. Koan's profiles are
  containers inside one Chromium profile, so it has to separate history itself.

## What Chromium offers (158)

- **One `HistoryService` per browser profile.** A visit is added by `HistoryTabHelper::DidFinishNavigation()`
  → `CreateHistoryAddPageArgs()` → `HistoryService::AddPage()`. The tab is known there, and so is its
  partition (`containers::ContainerOf()`).
- **A per-visit tag already exists: `visits.app_id`.** It is a `TEXT` column (`visit_database.cc`) that
  Android uses for the app that opened a Custom Tab (`HistoryTabHelper::app_id_`, set through JNI only, and
  `#if BUILDFLAG(IS_ANDROID)`). Desktop never sets it, so every desktop visit has `''`. Upstream already
  handles it everywhere a tag must be handled:
  - `HistoryAddPageArgs::app_id` is stored for every visit of the navigation, redirects included
    (`HistoryBackend::AddPage` → `AddPageVisit`).
  - `QueryOptions::app_id` filters `QueryHistory()`, both the basic and the text query
    (`VisitDatabase::GetVisibleVisitsForURL` / the shared where-clause builder: `visits.app_id = ?`).
  - `ExpireHistoryBetween(restrict_urls, restrict_app_id, begin, end)` deletes one tag's visits, and drops
    a URL row when no visit is left. The delete directive handler and `BrowsingHistoryService`
    (chrome://history) take an app id too.
  - `URLResult::app_id()` and `VisitRow::app_id` carry it back. `GetAllAppIds()` lists the tags.
  - The schema and its migrations are upstream's.
- **The omnibox has no per-visit data.** `HistoryQuickProvider` reads `InMemoryURLIndex` (URL rows and their
  last visits). `HistoryURLProvider` reads the in-memory URL database and, on its async pass, the URL
  table. `ShortcutsProvider` reads the shortcuts database, which records what was picked in the omnibox and
  not where. `SearchProvider`'s past searches come from `keyword_search_terms`, keyed by URL row. None of
  them can filter by tag.
- **`AutocompleteController::UpdateResult()`** gathers every provider's matches (`AggregateNewMatches()`)
  before sorting, culling and choosing the default match. It is the one place every suggestion passes
  through. `AutocompleteResult::EraseMatchesWhere()` is public.
- **Switch to Tab** (`has_tab_match`, the open-tab provider, the tabs search) asks
  `TabMatcherDesktop::GetOpenWebContents()`, which Koan already patches (agent tabs, the empty tab).
- **Koan's Library** reads history with `QueryHistory()` (`koan_library_history.cc`), so its filter is
  Koan's own code.

## Decisions

| Question | Decision | Why |
|---|---|---|
| Where the profile lives | **Upstream's `visits.app_id`**, as `"koan:" + Serialize(container)` (`koan:user:s...`) for a saved user container, nothing for the default profile. | No schema change, no migration, no Koan database to keep in step with deletions and expiry: a visit's tag goes with the visit. Querying and deleting by tag are upstream code paths. A side table keyed by visit id would need its own file (`HistoryDatabase::GetDB()` is private), orphan sweeps and id-reuse guards. |
| Which visits are tagged | Every visit of a tab in a saved user container, whatever the setting. Untagged: the default partition, agent containers and agent tabs (their history is kept out, unless the user lets agents use it, and then it stays the default's, as before), Incognito (no history). | Turning the setting on later then sorts old visits right. |
| Visits from before #474, imported (#348) and synced history | No tag: the default profile's. | The issue's rule. `AddPagesWithDetails()` and sync set none. |
| Upstream setting an app id | Kept (`VisitAppId()` only fills an empty one). A tag that isn't Koan's counts as the default profile's. | Android only today; a desktop use would show in the REBASE guard. |
| Setting | **Keep history apart for each profile** (Tab Management > Workspaces), `koan.workspaces.separate_history`, **on by default**. Off: the URL bar and the Library show everything, as before. | The screen-sharing case is the point of the feature. Without profiles nothing changes, since every visit is the default's. |
| The URL bar: filter after or index per profile | **After the fact**: one hook in `UpdateResult()` drops the history suggestions of pages with no visit in the profile shown (`koan::FilterSpaceHistoryMatches()`). | An index per profile would mean a second `InMemoryURLIndex`, HQP and HUP per container and patches through their scoring. The cost of filtering after: a provider that returned its 3-5 best rows may leave fewer, and ranking still uses every profile's visit counts (it never shows a page from another profile). |
| What counts as a history suggestion | The matches of `HistoryQuickProvider`, `HistoryURLProvider`, `HistoryFuzzyProvider`, `ShortcutsProvider`, the zero-prefix local history and most-visited providers, history clusters and embeddings (both off), `SearchProvider`'s past searches (`kSearchHistory`), and the empty URL bar's top sites (Koan's zero-suggest provider). Not: what-you-typed, bookmarks, search suggestions, Koan's actions. | `IsSpaceHistoryMatch()`, unit tested. |
| How a suggestion finds its page | By the omnibox's own dedupe key (`GURLToStrippedGURL()`: no `www.`, http for https, a search page down to its terms). | A past search, a shortcut and the visit to the results page are the same page. |
| The address being typed | Kept when it is a history match (`HistoryURLProvider` makes the typed URL one), with its title cleared if the page isn't the profile's. | Otherwise typing a full address another profile visited would turn it into a search. |
| The index | `SpaceHistoryIndex`, one per browser profile (Profile user data): only pages with a tagged visit, each with its profiles and whether it also has an untagged visit. Read once with a `HistoryDBTask` (`GetAllAppIds()`, `GetAllVisitsInRange(tag)`, `GetVisitsForURL()`, all public), then kept from `OnURLVisited()`; any deletion reads it again (debounced 1 s). | Small: pages visited in profiles, not all history. |
| Which profile the URL bar shows | The active Space's profile of the last active window of that browser profile (the window being typed in); in a window without Spaces (Incognito, a New Blank Window) its active tab's container. A Space without a profile shows the default's. | The URL bar of a Space, not of a tab: a tab moved in from another Space still shows the Space's suggestions. Typing a URL there opens it in that tab's own container as before. |
| Switch to Tab | The tab matcher leaves out tabs of another profile (`IsOutsideOmniboxProfile()`): Switch to Tab buttons, the empty URL bar's open tabs and the tabs search (Shift+Cmd+A). | Same screen-sharing case. |
| Library > History | A **Which profile?** filter after the when, sort and source filters: All profiles, Default, then each profile. Each opening picks the active Space's profile. It filters with `QueryOptions::app_id` (`''` for Default), and closed tabs by the container in their session data. Only with the setting on and at least one profile. | |
| Forget This Page, Forget About This Site, Clear history | Act on the profile the filter shows, and say so ("... in the Work profile", "... in every profile"). One profile: `ExpireHistoryBetween(urls or range, its tag)`; the site's cookies, storage and cache in that profile's partition only (`SetStoragePartitionConfig`, partition data types only). All profiles: upstream's deletes as before, plus the site's data in every profile's partition. Default: the default partition, with the site's permissions and engagement (shared by every profile), as before. | "Can act on all": pick All profiles. |

## Hard cases

| Case | Decision |
|---|---|
| A page visited in two profiles | It shows in both, each with its own visits (the Library lists each profile's visit times). |
| A tab moved to another Space | It keeps its profile, so its visits stay with that profile. Its URL bar shows the new Space's suggestions. |
| Agent tabs | Their history is already kept out. Unchanged; when the user lets agents use history, their visits are the default profile's, as before. |
| Top sites on the empty URL bar | Per profile too: Chromium's `TopSites` is one list, so Koan drops the sites with no visit in the profile shown. Fewer rows, never another profile's. |
| Imported history (#348) | The default profile's. |
| Clear browsing data (Chromium's dialog) | Clears every profile's history (it deletes without a tag). Its dialog is upstream WebUI and doesn't say so; USAGE.md and the setting do. The Library's Clear history says which profile it acts on. |
| A deleted profile's visits | Keep their tag. They show under All profiles only, and in no Space's URL bar. Deleting a profile and its data is container management's (a follow-up of #318). |
| chrome://history | Upstream: every profile's visits. It groups a page's visits of one day per tag, so a page visited in two profiles can show twice. |
| Bookmarks, passwords, autofill | Shared, as now. A bookmarked page still shows as a bookmark suggestion in every Space. |
| Incognito | No history of its own. Its URL bar shows the default profile's history suggestions. |
| History sync | Not in Koan (no Google sync). Upstream would sync the tag. |
| Suggestions while the index loads | Right after start, for a moment: everything in the default profile's URL bar, no history suggestion in another's. |
| Address variants | Known limit: right after start, a page's `http`/`www` variant visited only in the default profile counts with the variant visited in a profile under the same key, so the default profile's URL bar may hide it until it is visited again there. |

## Hooks

| Patch | Change | Size |
|---|---|---|
| `chrome/browser/history/history_tab_helper.cc` | Existing patch, one new hunk in `CreateHistoryAddPageArgs()`: `add_page_args.app_id = koan::containers::VisitAppId(web_contents(), add_page_args.app_id);` and an include. No new BUILD dep (`:partition` already). | +4 lines |
| `components/omnibox/browser/autocomplete_controller.cc` | Existing patch, one new hunk after `AggregateNewMatches()`: `koan::FilterSpaceHistoryMatches(*provider_client_, input_, internal_result_);` (declared in `koan_omnibox_hooks.h`, already included and depended on). | +2 lines |
| `chrome/browser/autocomplete/tab_matcher_desktop.cc` | Existing hunk: one more condition next to the agent check, and an include. `//koan/browser/ui/views` is already a dep. | +3 lines |

No new patch. No extension point fits: the visit's args are built inside `HistoryTabHelper` with no
delegate, the controller has no match filter, and the tab matcher has no observer.

**Rebase guards** (rule 3, named in the REBASE rows):
- `KoanSpaceHistoryBrowserTest.VisitsRecordTheirProfile`: a default tab's visit has no tag and a profile's
  has its own. It fails if upstream starts setting `app_id` on desktop, or stops storing it.
- `SuggestionsComeFromTheSpacesProfile`: the omnibox's result passes through the hook.
- `LibraryFilterActsOnOne`: `QueryOptions::app_id` and `ExpireHistoryBetween(restrict_app_id)` act on one
  tag.

## Tests

- Unit: `HistoryProfilesTest` (`koan_containers_unittests`: the tag, its parsing, the page rule),
  `KoanHistoryMatchesTest` (`koan_omnibox_unittests`: which matches are history, the page key, the typed
  address).
- Browser: `koan_space_history_browsertest.cc` (above, and Switch to Tab per profile, with a moved tab, on
  and off).
- Hands-on: `scripts/mac/steps/space_history.md`, and the PR's Linux steps.
