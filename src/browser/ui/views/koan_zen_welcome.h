// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   ZenWelcome.mjs
//   zen-welcome.ftl
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

// The welcome screen's words, its pages and their order, the Essentials it
// offers and the search engines it leaves out.

#ifndef KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_WELCOME_H_
#define KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_WELCOME_H_

#include <array>

#include "third_party/skia/include/core/SkColor.h"

namespace koan {

// zen-welcome.ftl.
inline constexpr char16_t kWelcomeTitleLine1[] = u"Welcome to";
inline constexpr char16_t kWelcomeTitleLine2[] = u"a calmer internet";
inline constexpr char16_t kWelcomeBack[] = u"Back";
inline constexpr char16_t kWelcomeNext[] = u"Next";
inline constexpr char16_t kWelcomeSkip[] = u"Skip for now";
inline constexpr char16_t kWelcomeDiveIn[] = u"Dive in!";
inline constexpr char16_t kWelcomeYes[] = u"YES";
inline constexpr char16_t kWelcomeNo[] = u"NO";
inline constexpr char16_t kWelcomeImportYes[] = u"import from another browser.";
inline constexpr char16_t kWelcomeImportNo[] = u"start fresh.";
inline constexpr char16_t kWelcomeDefaultBrowserYes[] = u"make Koan default.";
inline constexpr char16_t kWelcomeDefaultBrowserNo[] =
    u"still exploring my options.";

// ZenWelcome.mjs's pages, in its order (without the ad blocker).
enum WelcomePage {
  kWelcomePageImport,
  kWelcomePageColors,
  kWelcomePageSearch,
  kWelcomePageEssentials,
  kWelcomePageDefaultBrowser,
  kWelcomePageFinish,
  kWelcomePageCount,
};

struct WelcomePageText {
  const char16_t* title;
  const char16_t* description;
};

inline constexpr std::array<WelcomePageText, kWelcomePageCount>
    kWelcomePageText = {{
        {u"Bring your stuff along.",
         u"Import your bookmarks, history and passwords from another browser "
         u"and pick up right where you left off."},
        {u"Your space, your colors.",
         u"Personalize your browser by giving each space its own unique "
         u"color identity."},
        {u"Pick your search engine.",
         u"Choose your default search engine. You can always change it "
         u"later!"},
        {u"Choose the apps you use most.",
         u"Select your favorite apps to keep them handy in your sidebar."},
        {u"Make Koan your default?",
         u"Links from other apps will open in Koan. You can always change "
         u"this later."},
        {u"All set? Let’s get rolling!",
         u"You’re all set up and ready to go. Click the button below to "
         u"start browsing with Koan."},
    }};

// ZenWelcome.mjs's Essentials: the site, its name and its colour.
struct WelcomeEssentialApp {
  const char* url;
  const char16_t* name;
  SkColor color;
};

inline constexpr auto kWelcomeEssentialApps =
    std::to_array<WelcomeEssentialApp>({
        {"https://obsidian.md", u"Obsidian", SkColorSetRGB(0x7c, 0x3a, 0xed)},
        {"https://discord.com", u"Discord", SkColorSetRGB(0x58, 0x65, 0xf2)},
        {"https://trello.com", u"Trello", SkColorSetRGB(0x00, 0x52, 0xcc)},
        {"https://slack.com", u"Slack", SkColorSetRGB(0xe0, 0x1e, 0x5a)},
        {"https://github.com", u"GitHub", SkColorSetRGB(0x57, 0x60, 0x6a)},
        {"https://app.tuta.com/", u"Tuta", SkColorSetRGB(0xc0, 0x00, 0x02)},
        {"https://notion.com", u"Notion", SkColorSetRGB(0x37, 0x35, 0x2f)},
        {"https://calendar.google.com", u"Calendar",
         SkColorSetRGB(0x42, 0x85, 0xf4)},
        {"https://figma.com", u"Figma", SkColorSetRGB(0xf2, 0x4e, 0x1e)},
    });

// The top edge of Zen's welcome video (zen-videos/welcome-background.mp4),
// averaged over its loop: on macOS the title bar row beside and behind the
// traffic lights takes it while chrome://koan-welcome plays the video below.
inline constexpr SkColor kWelcomeVideoTopColor = SkColorSetRGB(160, 196, 125);

// Search engines the search page leaves out (by name, lower case).
inline constexpr auto kWelcomeExcludedEngines =
    std::to_array<const char16_t*>({u"wikipedia", u"ebay"});

}  // namespace koan

#endif  // KOAN_BROWSER_UI_VIEWS_KOAN_ZEN_WELCOME_H_
