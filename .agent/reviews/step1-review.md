# Step 1 (Skeleton) review — Koan feature flags + about_flags hook

Revision under review: Chromium `158.0.8111.0` @ `67e29b8287f1`. Build args
(`out/Koan/args.gn`): `is_component_build=true`, `dcheck_always_on=true`,
`is_debug=false` — so DCHECKs and the `-Wglobal-constructors`-as-error pragma
both apply. Working tree already has both step‑1 patches applied; the two
patch files match the current `git diff` byte-for-byte.

## Findings

- **nit — `BASE_DECLARE_FEATURE` now carries `extern "C"`; header is correct but note linkage.**
  Evidence: `base/feature.h:48` (`#define BASE_DECLARE_FEATURE(kFeature) extern "C" constinit const base::Feature kFeature`).
  `koan_features.h:13,16` use the declare macro and `koan_features.cc:9-10` use the
  2‑arg `BASE_FEATURE(kKoanSidebar, base::FEATURE_ENABLED_BY_DEFAULT)` (`base/feature.h:97`,
  `base/feature_internal.h:70-82`), which is exactly upstream's pattern. Names start
  with `k` so the macro's `static_assert(#feature[0]=='k')` passes. No fix needed;
  just be aware the emitted symbol is unmangled `kKoanSidebar` (see export finding below).

- **pass — macro revision match.** `koan_features.h:8` includes `base/feature_list.h`,
  which includes `base/feature.h` (`base/feature_list.h:23`), so `BASE_DECLARE_FEATURE`
  and `BASE_FEATURE` resolve. `base::FEATURE_ENABLED_BY_DEFAULT` is
  `base::FeatureState` (`base/feature.h:175-177`) and the `IsCountrySpecificFeatureState`
  static_assert in the macro accepts it. Correct for this revision.

- **pass — `KOAN_FEATURE_ENTRIES` initialiser matches `FeatureEntry` layout.**
  Field order is `internal_name, visible_name, visible_description, supported_platforms,
  type, union, links` (`feature_entry.h:165-246`). `FEATURE_VALUE_TYPE` expands to
  `type, { .feature = { &feature_entry, {}, nullptr } }` (`feature_entry_macros.h:38-41`),
  giving the 6-element aggregate; `links` is value-initialised. `supported_platforms`
  is `unsigned short` (`feature_entry.h:175`) initialised from `const unsigned kOsDesktop`
  — a constant expression that fits, so no narrowing error (identical to every existing entry).

- **pass — `kOsDesktop` is in scope at the expansion point.**
  Defined at `chrome/browser/about_flags.cc:446` inside `namespace about_flags { namespace {`
  (starts at `:447`); `KOAN_FEATURE_ENTRIES` expands at `:14514`, in the same anonymous
  namespace. The `#include` at `:239` precedes the definition but that is fine because the
  macro body is not expanded until `:14514`.

- **pass — no `-Wglobal-constructors` risk.**
  `chrome/browser/about_flags.cc:428` makes the warning an error here (guarded only for
  `IS_WIN && COMPONENT_BUILD`; on Linux it is active). Every element of `kFeatureEntries`
  is an address constant plus `base::span{}`; `base::span()` is `constexpr`
  (`base/containers/span.h:520,1067`), so the array is constant-initialised into
  `.data.rel.ro` and no global constructor is emitted. Same machinery as all existing entries.

- **pass — no runtime DCHECK/ordering/expiry rejection of the new entries.**
  `FlagsState`'s constructor just stores the span (`components/webui/flags/flags_state.cc:348`);
  no sort/uniqueness validation runs at construction. `IsSupportedFeature` does
  `DCHECK(entry.IsValid())` (`flags_state.cc:1110-1111`) and `IsValid()` for
  `FEATURE_VALUE` only requires a non-null `feature.feature` (`feature_entry.cc:198+`,
  the `FEATURE_VALUE` case), which the entries satisfy. `flags::IsFlagExpired` returns
  `false` for any name not in the generated expiry table (`chrome/browser/unexpire_flags.cc:74-79`),
  and there is no DCHECK that the name exists in `flag-metadata.json`. Flag-name ordering
  and metadata existence are checked only in unit tests (`chrome/browser/about_flags_unittest.cc:139`
  `EveryFlagHasMetadata`; also `AboutFlagsHistogramTest` against `tools/metrics/histograms/enums.xml:7789`).

- **should-fix — new flags absent from `flag-metadata.json` and `enums.xml`.**
  `grep` finds no `koan-sidebar` / `koan-compact-mode` in `chrome/browser/flag-metadata.json`
  or `tools/metrics/histograms/enums.xml`. This is not a build or dcheck failure, but it will
  fail `AboutFlagsTest.EveryFlagHasMetadata` and `AboutFlagsHistogramTest` the moment anyone
  runs the about_flags tests on the trybots. Design §2 lines 104-107 defers this to Phase 4;
  that is acceptable for step 1, but add the two entries (metadata expiry + `LoginCustomFlags`
  enum) in the same change that first runs those tests.

- **pass (with a caveat) — component-build symbol visibility.**
  `//koan/browser:features` is a `source_set` (`src/browser/BUILD.gn:7-13`) and every
  `//chrome/browser` target here is a `source_set` too (`chrome/browser/BUILD.gn:999` is the
  only `source_set("core")`; no `component()` in that file). All of them are linked into the
  same final link unit, so a non-exported `koan::features::kKoanSidebar` (or the non-inline
  `IsSidebarEnabled()`) referenced from other `//chrome/browser/*` targets links fine — no
  `COMPONENT_EXPORT`/`BASE_EXPORT` needed *today*. Caveat: `BASE_DECLARE_FEATURE` is
  `extern "C"`, so if a Koan feature is ever referenced from a real `component()` target
  (a separate shared library) it must be declared `COMPONENT_EXPORT(MODULE)
  BASE_DECLARE_FEATURE(...)` (see `base/feature.h:45-48`). Leave as-is for step 1.

- **pass — BUILD.gn wiring.** The dep `"//koan/browser:flag_entries"` is inserted at
  `chrome/browser/BUILD.gn:1433`, inside `source_set("core")` (starts `:999`, and
  `about_flags.cc` is listed at `:1001`), in correct alphabetical position between
  `//device/...` and `//media/...`. `:flag_entries` lists `koan_flag_entries.h` in `sources`
  and `public_deps` `:features` + `//components/webui/flags` (`src/browser/BUILD.gn:17-23`),
  so `gn check` will accept `about_flags.cc`'s transitive includes of
  `koan_features.h`/`feature_entry_macros.h`.

- **nit — `:flag_entries` is a `source_set` with only a header** (`src/browser/BUILD.gn:17-23`).
  It compiles nothing; a `group` (or folding the dep into `:features`) is tidier and equally
  `gn check`-friendly. Also the `KOAN_FEATURE_ENTRIES` macro is never `#undef`'d — harmless.

- **nit — include placement.** `about_flags.cc:239` drops the Koan include between the
  `gpu/...` and `media/...` blocks; it is alphabetically fine within the flat block, but the
  file uses grouped blocks and PRESUBMIT's include-order check may flag it. Cosmetic.

### `scripts/apply_patches.sh`

- **pass — symlink resolution.** `ln -sfn ../../src koan` run with cwd `chromium/src`
  (line 21 `cd "$SRC"`) targets `~/work/koan/src`; the on-disk link
  `chromium/src/koan -> ../../src` is correct. `/koan` is present in `.git/info/exclude`.
  `patch_files()` paths, `rel="${p#"$PATCHES"/}"`, and the `apply/check/reverse` mode
  dispatch all behave.

- **should-fix — `export` silently ignores staged changes.** `git diff --name-only`
  (line 91) and `git diff ... -- "$f"` (line 89) compare the working tree to the **index**,
  not to `HEAD`. A hook patch the developer has `git add`ed is exported as empty/missing,
  and `patches/` is then wiped. Use `git diff HEAD --name-only` / `git diff HEAD -- "$f"`.

- **should-fix — `export` destroys all patches before proving there is anything to write.**
  Lines 84-85 `rm -rf "$PATCHES"; mkdir -p "$PATCHES"` run first; if the tree is clean
  (e.g. after `reverse`) the loop writes nothing and every patch file is gone with no error.
  Export to a temp dir and swap in, or `exit 1` early if `git diff --name-only` is empty.

- **should-fix — missing `$PATCHES` is reported as success.** `find "$PATCHES" ...` runs in a
  process substitution (lines 54/70/81); its failure does not trip `set -e`, so the loop body
  never executes and `apply`/`check` `exit 0`. Add an explicit
  `[[ -d "$PATCHES" ]] || { echo "no patches dir" >&2; exit 1; }` and abort if zero patches
  were found.

- **nit — whitespace/quoting in the read loops.** `while read -r p` (lines 42/59/74/86) strips
  leading/trailing whitespace, and `git diff --name-only` (default `core.quotePath=true`)
  emits C‑quoted paths for names with spaces/specials, which the loop cannot open. Chromium
  paths never contain these, so it is theoretical; `IFS= read -r` plus
  `git -c core.quotePath=false` (or `-z`) would make it robust.

- **nit — `git apply "$p"` (line 48) is unguarded under `set -e`.** If it fails despite
  `can_apply` succeeding, the script exits mid-loop instead of reporting `CONFLICT` and
  continuing; inconsistent with the `check` mode's error handling.

- **nit — `link_koan` robustness.** It only creates the link when `! -L koan` (line 24), so a
  stale link pointing at the wrong place is left alone, and if `koan` exists as a real
  directory `ln -sfn` fails under `set -e`. Appending to `.git/info/exclude` (line 28) also
  fails if `.git` is a worktree file. Low impact for this repo layout.

## Verdict

The C++ and GN side of step 1 is correct for this exact revision and should compile and run:
the feature macros match `base/feature.h` (including the `extern "C"` declaration), the
`KOAN_FEATURE_ENTRIES` aggregate matches the `FeatureEntry` layout with `kOsDesktop` in scope,
the entries are constant-initialised so the `-Wglobal-constructors`-as-error pragma in
`about_flags.cc` will not fire, and none of the runtime DCHECK/expiry paths in
`flags_state.cc`/`unexpire_flags.cc` reject them; the component-build linkage is also fine
because all `//chrome/browser` targets share the final link. The only functional follow-ups
are test-facing (adding the two flags to `flag-metadata.json` and `enums.xml`) and one
robustness cluster in `apply_patches.sh`'s `export` mode (staged-change blindness, wipe-before-
verify, and silent success when `patches/` is missing); everything else is cosmetic.
