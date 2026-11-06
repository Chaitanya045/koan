#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Fresh test profiles for the Spaces lockdown's by-hand tests (PR #159,
.agent/notes/upstream/tab-groups-omnibox-fullscreen.md section 1.6).

  python3 scripts/mac/spaces/seed_profiles.py <out-dir>

Writes one user data dir per test under <out-dir> (never the user's own
profile; nothing is installed). Tests 4 and 12 are seeded completely from
prefs. Tests 6 and 11 need tab groups that only a running browser can write
into its session and saved-groups files, so their dir gets the prefs and the
script prints the seeding run: Koan with its Spaces turned off
(--disable-features=KoanWorkspaces, so upstream keeps the groups as Chrome
would: collapsed, coloured, saved) and the unpacked test extension
(scripts/mac/spaces/test-extension), whose seed action builds the groups.
README.md in this directory says what each test then expects.
"""
import json
import os
import sys

# tab_groups::TabGroupColorId.
GREY, BLUE, RED, YELLOW, GREEN = 0, 1, 2, 3, 4
# ui::mojom::BrowserColorVariant.
TONAL_SPOT = 1
# SessionStartupPref (chrome/browser/prefs/session_startup_pref.h).
STARTUP_LAST, STARTUP_URLS, STARTUP_NEW_TAB = 1, 4, 5


def argb(rgb):
    """An opaque SkColor as the signed int prefs store."""
    value = 0xFF000000 | rgb
    return value - (1 << 32) if value >= 1 << 31 else value


# KoanWorkspaceController's TintSeed() for red, and a user colour of their own.
SEED_RED = argb(0xD93025)
USER_GREEN = argb(0x0B8043)


def base_prefs(startup=STARTUP_LAST):
    return {
        # No welcome screen, no "Restore pages?" bubble, the session (or not)
        # as the test needs.
        "koan": {"welcome_screen": {"seen": True}},
        "profile": {"exit_type": "Normal", "exited_cleanly": True},
        "session": {"restore_on_startup": startup},
        "browser": {"has_seen_welcome_page": True},
    }


def write_profile(out, name, prefs):
    user_data = os.path.join(out, name)
    default = os.path.join(user_data, "Default")
    os.makedirs(default, exist_ok=True)
    with open(os.path.join(default, "Preferences"), "w") as f:
        json.dump(prefs, f, indent=1)
    # Skips Chrome's first run (default browser prompt, import).
    open(os.path.join(user_data, "First Run"), "w").close()
    return user_data


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = os.path.abspath(sys.argv[1])
    os.makedirs(out, exist_ok=True)
    koan = os.environ.get("KOAN_APP", "out/Koan/Koan.app/Contents/MacOS/Koan")
    ext = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "test-extension")

    # Test 4 (G1 migration): what an older Koan leaves after a red Space was
    # the active one: the profile's user colour is Koan's red tint, and the
    # user's own (green, tonal spot) is kept in koan.workspaces.tint_saved_*.
    prefs = base_prefs()
    prefs["browser"]["theme"] = {"user_color2": SEED_RED,
                                 "color_variant2": TONAL_SPOT}
    prefs["koan"]["workspaces"] = {"tint_applied": SEED_RED,
                                   "tint_saved_color": USER_GREEN,
                                   "tint_saved_variant": TONAL_SPOT}
    t4 = write_profile(out, "test4-tint-migration", prefs)

    # Test 12 (G9): startup set to the New Tab page, and the Spaces a window
    # stored: Home, "<rocket> Research" (blue, the one shown) and Play (red).
    prefs = base_prefs(STARTUP_NEW_TAB)
    prefs["koan"]["workspaces"] = {"list": [
        {"title": "Home", "color": GREY, "active": False},
        {"title": "\U0001F680 Research", "color": BLUE, "active": True},
        {"title": "Play", "color": RED, "active": False},
    ]}
    t12 = write_profile(out, "test12-stored-spaces", prefs)

    # Tests 6 and 11: prefs now, groups from a seeding run.
    t6 = write_profile(out, "test6-chrome-groups", base_prefs())
    t11 = write_profile(out, "test11-saved-groups", base_prefs())

    print("Seeded under", out)
    print()
    print("Test 4 (G1 migration):  %s --user-data-dir=%s" % (koan, t4))
    print("Test 12 (G9):           %s --user-data-dir=%s" % (koan, t12))
    for name, user_data, action in (("6", t6, "seed6"), ("11", t11, "seed11")):
        print()
        print("Test %s: seed it once, then run it:" % name)
        print("  1. %s --user-data-dir=%s --disable-features=KoanWorkspaces "
              "--load-extension=%s" % (koan, user_data, ext))
        print("     In that window open the extension's service worker console "
              "(chrome://extensions > Koan Spaces test extension > service "
              "worker) and run:  run('%s')" % action)
        print("     Wait for 'koan-spaces-ext %s done' in the console, then "
              "quit with Cmd+Q." % action)
        print("  2. %s --user-data-dir=%s" % (koan, user_data))


if __name__ == "__main__":
    main()
