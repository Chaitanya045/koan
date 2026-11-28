# Arc fixture: two profiles, link rules (#481)

An Arc folder as `--koan-import-test-root=<this folder>` expects it
(`<root>/Arc/User Data`, `StorableSidebar.json` and
`StorableLinkRouting.json` beside it). Structure only: every name and URL is
made up. It was written from the formats in
`.agent/notes/upstream/importer.md` (D5, D6, D13), not copied from a real
Arc install, so the parts marked "assumed" there are assumed here too.

- Two profiles (Local State): `Default` "Personal" and `Profile 1` "Work",
  each with a bookmark on its bar. No History or Login Data (SQLite files);
  the browser test and the e2e fixtures write those.
- Spaces: Home (Personal, 🏠), Projects (Work, 🛠️, a pinned tab and a
  folder "Specs"), Clients (Work). One Favorite per profile. An unpinned
  tab, which is never imported.
- Link rules: `git.example` contains → Projects; `mail.example/inbox` is →
  Home (the nested Space id form); links from other apps → Home. Left out
  on import: a Little Arc rule, a "most recent Space" rule, a rule with two
  conditions and a "starts with" rule.

What an import of "Arc — All profiles" with Spaces and bookmarks should
make: profiles "Work" (new; Personal is Koan's Default), Spaces Home
(Default), Projects and Clients (Work), Essentials "Personal favorite"
(Default) and "Work favorite" (Work), two Space routes and the external
default Home, and the bookmarks in folders "Personal" and "Work".
