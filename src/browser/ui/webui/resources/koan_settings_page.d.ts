// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Koan's panes in chrome://settings (koan_settings_page.js), for Chromium's
// settings TypeScript build: settings_main.ts imports the module (hook patch),
// which the browser serves outside the settings bundle (koan_settings_ui.cc;
// excluded from the bundle in chrome/browser/resources/settings/BUILD.gn). It
// defines <koan-settings-page> and exports nothing.
declare module 'chrome://settings/koan_settings_page.js' {}
