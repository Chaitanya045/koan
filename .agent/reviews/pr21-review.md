# PR #21 Review — Workspace icons (emoji) like Zen

Scope: `src/browser/ui/views/koan_workspaces.cc`, BUILD.gn, docs, e2e script. Chromium APIs cross-checked against `~/work/koan/chromium/src` (break_iterator.h/.cc, char_iterator.h, metadata macros, type_converter.h).

## Findings

### 1. Low — `emoji_` heuristic misclassifies the "∞" button and non-Latin initials
`koan_workspaces.cc:146` — `emoji_(!label_.empty() && label_.front() > 0x2000)`.

- The All-tabs button label `∞` is U+221E (0x221E > 0x2000) → drawn with font delta **+1** instead of the previous −1; same for any CJK initial (e.g. `工`, U+4E00) and general punctuation (`–` U+2013, quotes, `…`). Failure scenario: a workspace renamed to a CJK word shows an oversized character that can clip inside the fixed 24 px circle (`CalculatePreferredSize`, line 154).
- It only affects font sizing, not logic, so it's polish. Fix: reuse the `UCHAR_EXTENDED_PICTOGRAPHIC` check from `SplitIcon` (hoist it into a small `bool IsPictographic(const std::u16string&)` helper) instead of the code-unit threshold.

### 2. Low — leading whitespace defeats the icon split
`koan_workspaces.cc:85-99` — `SplitIcon` runs the grapheme iterator on the raw name. For `" 🌊 Work"` the first grapheme is the space, which is not pictographic, so the function returns `{ " ", " 🌊 Work" }`: the button renders a blank icon and the emoji is never recognized. Failure scenario: user renames a workspace with an accidental leading space and gets a visually empty switcher button. Fix: `base::TrimWhitespace(name, base::TRIM_LEADING, &name)` before creating the iterator.

### 3. Informational — text-presentation pictographs count as icons
`koan_workspaces.cc:92-94` — `©` (U+00A9), `®` (U+00AE), `™` (U+2122), `‼`, `ℹ`, `⚠` etc. have `Extended_Pictographic=Yes` even though they normally render as text. A name like `"© 2026 Acme"` splits to icon `©` / name `2026 Acme`. This is a defensible reading of "a leading emoji is the icon" and renders fine in the circle; only flag it if you want strict emoji-presentation semantics (would need `Emoji_Presentation=Yes` **or** `Emoji=Yes` followed by VS16). No change requested.

## Verified correct (no action)

- **Keycap/digit/# trap avoided**: `0-9`, `#`, `*` have `Emoji=Yes` but `Extended_Pictographic=No`, so `"#tags"` / `"1. Todo"` correctly keep the whole name and don't split (only `Extended_Pictographic` and `Regional_Indicator` are consulted, lines 92-94). The `UCHAR_REGIONAL_INDICATOR` check is necessary and correct: RI is *not* Extended_Pictographic, so flags like `🇮🇳 India` stay intact as one grapheme with a recognized icon.
- **BreakIterator usage**: `BREAK_CHARACTER` + `Init()`/`Advance()`/`GetString()` matches `base/i18n/break_iterator.h`; `GetString()` returns `[prev(), pos())` and the empty-string / `Init()`-failure paths (line 87) return safely. ICU's character iterator implements UAX #29 GB11, so ZWJ sequences (👨‍👩‍👧) and skin-tone modifiers stay one grapheme as the comment claims.
- **UTF16CharIterator on the first grapheme** (line 91): `first` is provably non-empty (a successful `Advance()` yields a non-empty cluster), so the default-constructed iterator's `get()` is valid; it returns a full code point (not a half surrogate), which is what `u_hasBinaryProperty` expects.
- **Emoji-only name** `"🌊"`: `rest` is empty after trimming → returns `{🌊, 🌊}`, so the button keeps the icon and the tooltip is the emoji itself rather than empty (line 100). Good.
- **Empty name**: `{"", ""}` → the `title.empty()` fallback in `Rebuild` (lines 238-241) assigns a number label and `Workspace N` tooltip, so no empty label ever reaches the button.
- **Lifetime in `SplitIcon`**: `BreakIterator` holds a `string_view` of the by-value `name` parameter; `GetString()` is copied into `first` before `name` is moved into the return value, and the moved-to string (SSO buffer or pilfered heap buffer) outlives the iterator's destruction. No use-after-free.
- **Metadata**: `ADD_READONLY_PROPERTY_METADATA(std::u16string, Label)` (line 202) expands to `ObjectPropertyReadOnlyMetaData<TheClass, std::u16string, decltype(GetLabel()), &GetLabel>` and requires a `GetLabel()` accessor — which exists (line 159) returning `const std::u16string&`. `TypeConverter<std::u16string>` is serializable (`base_type_conversion.h:274`), so the property is exposed read-only, matching what the new e2e script reads as the `Label` attribute.
- **BUILD.gn**: `//base:i18n` and `//third_party/icu` are valid targets (the latter is the `group("icu")` alias other targets already depend on).
- Default-name construction via `base::StrCat({style.icon, u" Space ", …})` is type-correct; the e2e expectations (`🌊 Space 1` → tooltip `Space 1`) match the implementation.

## Verdict

No blocking bugs — the Unicode heuristics are the tricky part and they hold up; only two low-severity polish items (font-size heuristic, leading-whitespace trim) are worth a follow-up.
