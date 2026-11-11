// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   zen-preferences.ftl (strings)
//   ZenKeyboardShortcuts.mjs (macOS key notation)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

// Koan's panes in chrome://settings, laid out like Zen's preferences: a
// SettingsPlugin (chrome/browser/resources/settings/settings_main/
// settings_plugin.ts) in the sections chrome://settings/koan/<pane>, so
// Chromium's menu lists them and its search finds them (koan_settings_ui.h).
// Everything shown comes from the browser (koan_settings_rows.cc and
// koan_shortcut_table.cc); add settings there, not here.

// Only chrome://resources modules the settings bundle keeps shared may be
// imported here (BUNDLE_JS_EXCLUDES, ui/webui/resources/tools/
// bundle_js_excludes.gni): the bundle has its own copy of every other one, and
// a second copy defines its elements twice (cr-iconset, 2026-11-12: the whole
// settings app failed to start).
import {addWebUiListener, sendWithPromise} from 'chrome://resources/js/cr.js';

// The page's router (chrome://settings/settings.js re-exports it). Imported
// dynamically: settings_main.ts imports this module (hook patch), so it runs
// before Chromium's settings app is defined, and the menu's koan: icons
// exist when the menu first draws them.
const settingsModule = import('./settings.js');

// Koan's sections are chrome://settings/koan/<pane> (route.ts hook).
const PANE_PATH = '/koan/';

let settings;
let currentPane;
// The plugin's shadow root, where the panes are drawn.
let root;
// The search, from Chromium's toolbar; '' shows the current pane.
let query = '';
// Settings > Agent Control > Saved containers (KoanAgentContainersHandler);
// null where agent control isn't built.
let agentContainers = null;
// Functions that bring the controls on screen up to date with the prefs.
let updaters = [];

// Creates an element. Children may be nodes or strings (text, never HTML).
function h(tag, props = {}, ...children) {
  const el = document.createElement(tag);
  for (const [key, value] of Object.entries(props)) {
    if (key === 'class') {
      el.className = value;
    } else if (key === 'dataset') {
      Object.assign(el.dataset, value);
    } else if (key.startsWith('on')) {
      el.addEventListener(key.slice(2), value);
    } else if (key.includes('-')) {
      el.setAttribute(key, value);
    } else {
      el[key] = value;
    }
  }
  el.append(...children.filter(child => child !== null));
  return el;
}

function prefValue(pref) {
  return settings.prefs[pref];
}

function updateAll() {
  for (const update of updaters) {
    update();
  }
}

// Settings panes ------------------------------------------------------------

function rowsOf(group) {
  return settings.rows.filter(row => row.group === group.id);
}

function paneHasContent(pane) {
  return pane.id === settings.shortcutsPane ||
      settings.groups.some(g => g.pane === pane.id && rowsOf(g).length);
}

async function setPref(row, value) {
  if (!await sendWithPromise('koanSetPref', row.pref, value)) {
    updateAll();
  }
}

function renderRow(row) {
  let el;
  const disabled = () => row.managed ||
      (row.dependsOn !== undefined && prefValue(row.dependsOn) !== true);
  switch (row.control) {
    case 'checkbox': {
      const input = h('input', {
        type: 'checkbox',
        onchange: () => setPref(row, input.checked),
      });
      el = h('label', {class: 'row checkbox'}, input, h('span', {}, row.label));
      updaters.push(() => {
        input.checked = prefValue(row.pref) === true;
        input.disabled = disabled();
      });
      break;
    }
    case 'menu': {
      const select = h(
          'select', {
            'aria-label': row.label,
            onchange: () => setPref(row, row.options[select.selectedIndex].value),
          },
          ...row.options.map(option => h('option', {}, option.label)));
      // The wrapper draws the chevron (koan_settings.css, .select).
      el = h('div', {class: 'row menu'}, h('span', {}, row.label),
             h('span', {class: 'select'}, select));
      updaters.push(() => {
        select.selectedIndex =
            row.options.findIndex(o => o.value === prefValue(row.pref));
        select.disabled = disabled();
      });
      break;
    }
    case 'textarea': {
      // A theme pasted from ui.shadcn.com/themes or tweakcn.com; the browser
      // checks it and keeps the old theme if it isn't one.
      const area = h('textarea', {
        'aria-label': row.label,
        spellcheck: false,
        placeholder: ':root {\n  --background: oklch(1 0 0);\n  --primary: ' +
            'oklch(0.205 0 0);\n}\n.dark {\n  --background: oklch(0.145 0 0);\n}',
      });
      const status = h('span', {class: 'theme-status', role: 'status'});
      const apply = async () => {
        const ok = await sendWithPromise('koanSetPref', row.pref, area.value.trim());
        status.classList.toggle('error', !ok);
        status.textContent = ok ?
            (area.value.trim() ? 'Theme applied' : 'Theme removed') :
            'That is not a theme: expected CSS with --background, ' +
                '--primary … in :root and .dark';
      };
      el = h('div', {class: 'row theme-paste'}, h('span', {}, row.label), area,
             h('div', {class: 'theme-actions'},
               h('button', {onclick: apply}, 'Apply'),
               h('button', {onclick: () => {
                 area.value = '';
                 apply();
               }}, 'Remove'),
               status));
      updaters.push(() => {
        if (root.activeElement !== area) {
          area.value = prefValue(row.pref) || '';
        }
        area.disabled = disabled();
      });
      break;
    }
    case 'layout': {
      const cards = row.options.map(
          option => h(
              'button', {
                class: 'layout-card',
                role: 'radio',
                dataset: {value: String(option.value)},
                onclick: () => setPref(row, option.value),
              },
              h('div', {class: `preview preview-${option.preview}`},
                h('div', {class: 'mock-window'}, h('div', {class: 'mock-toolbar'}),
                  h('div', {class: 'mock-body'},
                    h('div', {class: 'mock-sidebar'}, h('i'), h('i'), h('i'),
                      h('i'), h('i')),
                    h('div', {class: 'mock-page'})))),
              h('span', {}, option.label)));
      el = h('div', {class: 'row layout-list', role: 'radiogroup',
                     'aria-label': row.label}, ...cards);
      updaters.push(() => {
        cards.forEach((card, i) => {
          const selected = row.options[i].value === prefValue(row.pref);
          card.classList.toggle('selected', selected);
          card.setAttribute('aria-checked', String(selected));
          card.disabled = disabled();
        });
      });
      break;
    }
    default:
      return null;
  }
  updaters.push(() => el.classList.toggle('disabled', disabled()));
  el.classList.toggle('indent', row.indent);
  el.dataset.pref = row.pref;
  if (row.managed) {
    el.title = 'Set by your organization';
  }
  if (!row.note) {
    return el;
  }
  // Like Zen's warnings, e.g. zenTabsCycleByAttributeWarning.
  const note = h('p', {class: 'description row-note', dataset: {note: row.pref}},
                 row.note);
  updaters.push(() => {
    note.hidden = row.notePref !== undefined &&
        !(prefValue(row.pref) === true && prefValue(row.notePref) === true);
  });
  const fragment = document.createDocumentFragment();
  fragment.append(el, note);
  return fragment;
}

// The groups of `pane`, under their section headings. With `query`, only
// what matches it.
function renderGroups(pane, query) {
  const out = [];
  let section = null;
  for (const group of settings.groups.filter(g => g.pane === pane.id)) {
    let rows = rowsOf(group);
    if (query && !matches(query, group.header, group.description, group.section)) {
      rows = rows.filter(row => matches(query, row.label,
          ...row.options.map(option => option.label)));
    }
    if (!rows.length) {
      continue;
    }
    if (group.section !== section) {
      section = group.section;
      out.push(h('h1', {}, section));
    }
    out.push(h('section', {class: 'group', id: `group-${group.id}`},
               h('h2', {}, group.header),
               h('p', {class: 'description'}, group.description),
               ...rows.map(renderRow)));
  }
  return out;
}

function matches(query, ...texts) {
  return texts.some(text => text && text.toLowerCase().includes(query));
}

// Keyboard shortcuts ----------------------------------------------------------

const KEY_NAMES = {
  8: 'Backspace', 9: 'Tab', 13: 'Enter', 27: 'Esc', 32: 'Space',
  33: 'PageUp', 34: 'PageDown', 35: 'End', 36: 'Home', 37: '←', 38: '↑',
  39: '→', 40: '↓', 45: 'Insert', 46: 'Delete', 106: '*', 107: '+',
  109: '-', 110: '.', 111: '/', 186: ';', 187: '=', 188: ',', 189: '-',
  190: '.', 191: '/', 192: '`', 219: '[', 220: '\\', 221: ']', 222: '\'',
};

// Zen's display: Ctrl+Win+Alt+⇧+Key, and on macOS "⌃ ⌘ ⌥ ⇧ Key" (also
// shortcuts::DisplayText() in koan_shortcut_table.cc, for the command
// palette). ⌘ is the key Zen calls "accel" there.
function modifiersText(modifiers) {
  const m = settings.shortcuts.modifiers;
  const mac = settings.shortcuts.mac;
  return [
    [m.ctrl, '⌃', 'Ctrl'], [m.meta, '⌘', 'Win'], [m.alt, '⌥', 'Alt'],
    [m.shift, '⇧', '⇧'],
  ].filter(([flag]) => modifiers & flag)
      .map(([, glyph, name]) => mac ? `${glyph} ` : `${name}+`)
      .join('');
}

function keyName(code) {
  if (settings.shortcuts.mac) {
    const glyph = {13: '↩', 27: '⎋', 32: '␣'}[code];
    if (glyph) {
      return glyph;
    }
  }
  if ((code >= 48 && code <= 57) || (code >= 65 && code <= 90)) {
    return String.fromCharCode(code);
  }
  if (code >= 96 && code <= 105) {
    return `Num${code - 96}`;
  }
  if (code >= 112 && code <= 135) {
    return `F${code - 111}`;
  }
  return KEY_NAMES[code] ?? `#${code}`;
}

function shortcutText(key) {
  return modifiersText(key.modifiers) + keyName(key.key);
}

function sameKey(a, b) {
  return a.key === b.key && a.modifiers === b.modifiers;
}

// Like the browser's shortcut_table: a key with Ctrl, Alt or Win (⌘), or a
// function key.
function isValidKey(key) {
  const m = settings.shortcuts.modifiers;
  return (key.key >= 112 && key.key <= 135) ||
      (key.modifiers & (m.ctrl | m.alt | m.meta)) !== 0;
}

function isTabCycling(key) {
  return settings.shortcuts.tabCycling.some(k => sameKey(k, key));
}

function conflictOf(key, action) {
  return settings.shortcuts.actions.find(
      other => other.id !== action.id && other.keys.some(k => sameKey(k, key)));
}

function groupTitle(id) {
  return settings.shortcuts.groups.find(g => g.id === id)?.title ?? '';
}

// The shortcut being edited, as in Zen's gZenCKSSettings: type the keys,
// then Escape saves them; Backspace clears the shortcut; Tab leaves it.
const editor = {action: null, latest: null, saved: true};

function setNote(input, kind, text) {
  input.parentElement.querySelector('.shortcut-note')?.remove();
  if (kind) {
    input.after(h('div', {class: `shortcut-note ${kind}`}, text));
  }
}

// The action's current state (the list is replaced when shortcuts change).
function actionById(id) {
  return settings.shortcuts.actions.find(a => a.id === id);
}

function showKeys(input, action) {
  const key = actionById(action.id).keys[0];
  input.value = key ? shortcutText(key) : 'Not set';
  input.classList.toggle('not-set', !key);
}

function onShortcutFocus(input, action) {
  editor.action = action;
  editor.latest = null;
  editor.saved = true;
  input.classList.remove('invalid', 'unsaved');
  input.classList.add('editing');
  setNote(input, null);
  // Let every key reach the page, even the browser's own shortcuts.
  chrome.send('koanSetEditing', [true]);
}

// Saves `key` (what was typed) for `action`, or says why it can't be.
async function commitShortcut(input, action, key) {
  const conflict = conflictOf(key, action);
  let problem = null;
  if (conflict) {
    problem =
        `Conflicts with ${groupTitle(conflict.group)} -> ${conflict.label}`;
  } else if (isTabCycling(key)) {
    problem = settings.shortcuts.mac ?
        '⌃ Tab and ⌃ Page Up/Down switch tabs' :
        'Ctrl+Tab and Ctrl+Page Up/Down switch tabs';
  } else if (!isValidKey(key)) {
    problem = settings.shortcuts.mac ?
        'Use ⌃, ⌥ or ⌘ with the key, or a function key' :
        'Use Ctrl, Alt or Win with the key, or a function key';
  }
  if (problem) {
    input.classList.add('invalid');
    setNote(input, 'conflict', problem);
  } else if (await sendWithPromise('koanSetShortcut', action.id, key)) {
    input.classList.remove('invalid');
    input.classList.add('valid');
    setTimeout(() => input.classList.remove('valid'), 1000);
    if (settings.shortcuts.chromium.some(k => sameKey(k, key))) {
      setNote(input, 'replaces', 'Replaces Chromium\'s shortcut for this key');
    }
  } else {
    input.classList.add('invalid');
    setNote(input, 'conflict', 'This key can\'t be used for a shortcut');
  }
}

function onShortcutBlur(input, action) {
  chrome.send('koanSetEditing', [false]);
  input.classList.remove('editing');
  // Leaving the field with keys typed saves them too (Zen asks for Escape;
  // people click away).
  const unsaved = editor.saved ? null : editor.latest;
  editor.action = null;
  editor.latest = null;
  editor.saved = true;
  if (unsaved) {
    commitShortcut(input, action, unsaved);
  }
  // The field shows the key that is bound; a refused one stays explained by
  // its note.
  showKeys(input, action);
}

async function onShortcutKeyDown(event, input, action) {
  event.preventDefault();
  const m = settings.shortcuts.modifiers;
  const modifiers = (event.ctrlKey ? m.ctrl : 0) | (event.altKey ? m.alt : 0) |
      (event.shiftKey ? m.shift : 0) | (event.metaKey ? m.meta : 0);
  if (!modifiers && event.key === 'Tab') {
    editor.saved = true;
    input.blur();
    return;
  }
  // Escape saves the keys typed, as in Zen. So does a bare Enter, which would
  // otherwise replace them with "↩": a shortcut needs a modifier or an F-key,
  // so Enter alone is never one.
  if (!modifiers && (event.key === 'Escape' ||
                     (event.key === 'Enter' && editor.latest))) {
    if (editor.latest) {
      await commitShortcut(input, action, editor.latest);
    }
    editor.saved = true;
    input.blur();
    return;
  }
  if (!modifiers && event.key === 'Enter') {
    return;
  }
  if (!modifiers && event.key === 'Backspace') {
    editor.latest = null;
    editor.saved = true;
    input.classList.remove('invalid');
    input.value = 'Not set';
    input.classList.add('not-set');
    setNote(input, null);
    sendWithPromise('koanSetShortcut', action.id, null);
    return;
  }
  input.classList.remove('invalid', 'not-set');
  if (['Control', 'Alt', 'Shift', 'Meta', 'OS', 'AltGraph'].includes(event.key)) {
    input.value = modifiersText(modifiers);
    return;
  }
  editor.latest = {key: event.keyCode, modifiers};
  editor.saved = false;
  input.value = shortcutText(editor.latest);
}

function renderShortcut(action) {
  const input = h('input', {
    class: 'shortcut-input',
    id: `shortcut-${action.id}`,
    readOnly: true,
    dataset: {id: action.id},
    onfocus: () => onShortcutFocus(input, action),
    onblur: () => onShortcutBlur(input, action),
    onkeydown: event => onShortcutKeyDown(event, input, action),
  });
  updaters.push(() => {
    if (editor.action?.id !== action.id) {
      showKeys(input, action);
    }
  });
  return h('div', {class: 'shortcut'},
           h('label', {htmlFor: input.id}, action.label), input);
}

function renderShortcuts(query) {
  const groups = [];
  for (const group of settings.shortcuts.groups) {
    const actions = settings.shortcuts.actions.filter(
        a => a.available && a.group === group.id &&
            (!query || matches(query, a.label, group.title)));
    if (actions.length) {
      groups.push(h('section', {class: 'shortcut-group'},
                    h('h2', {}, group.title), ...actions.map(renderShortcut)));
    }
  }
  if (query) {
    return groups.length ? [h('h1', {}, 'Keyboard Shortcuts'), ...groups] : [];
  }
  return [
    h('h1', {}, 'Keyboard Shortcuts'),
    h('section', {class: 'group', id: 'shortcuts'},
      h('h2', {}, 'Customize your keyboard shortcuts'),
      h('p', {class: 'description'},
        'Change the default keyboard shortcuts to your liking and improve ' +
            'your browsing experience'),
      h('button', {
        class: 'reset-button',
        id: 'reset-shortcuts',
        onclick: () => {
          // Zen asks before resetting too.
          if (confirm('Reset all keyboard shortcuts to their defaults?')) {
            sendWithPromise('koanResetShortcuts');
          }
        },
      }, 'Reset to Default'),
      ...groups),
  ];
}

// Agent Control: saved containers --------------------------------------------

// The saved agent containers, with Add and Delete, and which agent client may
// use each one. Grants last until Koan quits or the clients' pairings are
// forgotten, like the pairings themselves.
function renderAgentContainers(query) {
  if (!agentContainers ||
      (query && !matches(query, 'saved containers', 'containers',
                         ...agentContainers.containers.map(c => c.name)))) {
    return [];
  }
  const granted = (client, container) => agentContainers.grants.some(
      ([c, id]) => c === client && id === container);
  const status = h('span', {class: 'theme-status'});
  const name = h('input', {
    type: 'text',
    class: 'container-name',
    maxLength: 64,
    placeholder: 'Name, e.g. GitHub bot',
    'aria-label': 'New saved container name',
  });
  const add = async () => {
    const value = name.value.trim();
    if (!value) {
      return;
    }
    if (await sendWithPromise('koanCreateAgentContainer', value)) {
      name.value = '';
      status.textContent = '';
    } else {
      status.textContent = 'That name is taken or not allowed';
    }
  };
  name.addEventListener('keydown', event => {
    if (event.key === 'Enter') {
      add();
    }
  });
  const containers = agentContainers.containers.map(container => h(
      'div', {class: 'row saved-container'},
      h('div', {class: 'saved-container-header'},
        h('span', {class: 'saved-container-name'}, container.name),
        h('button', {
          class: 'reset-button',
          onclick: () => {
            if (confirm(`Delete “${container.name}”? Its logins, cookies ` +
                        'and site data are deleted too.')) {
              sendWithPromise('koanDeleteAgentContainer', container.name);
            }
          },
        }, 'Delete')),
      ...(agentContainers.clients.length ?
              agentContainers.clients.map(client => {
                const input = h('input', {
                  type: 'checkbox',
                  checked: granted(client.id, container.id),
                  onchange: () => sendWithPromise(
                      'koanSetAgentContainerGrant', client.id, container.id,
                      input.checked),
                });
                return h('label', {class: 'row checkbox indent'}, input,
                         h('span', {}, `${client.label || 'Unnamed agent'}` +
                                           (client.connected ? '' :
                                                ' (not connected)')));
              }) :
              [h('p', {class: 'description'},
                 'Connect an agent to choose which may use it.')])));
  return [
    h('section', {class: 'group', id: 'group-agent-containers'},
      h('h2', {}, 'Saved containers'),
      h('p', {class: 'description'},
        'An agent works in a new, empty container unless you give it a ' +
            'saved one, which keeps its logins between tasks. Only the ' +
            'agents you tick here may use a saved container, and only when ' +
            'they ask for it by name. Your choices last until Koan quits or ' +
            'you stop all agents.'),
      ...containers,
      h('div', {class: 'row theme-paste'},
        h('div', {class: 'theme-actions'}, name,
          h('button', {onclick: add}, 'Add'), status))),
  ];
}

function renderPane(pane, query) {
  // Chromium's menu lists every pane (settings_menu.html hook); one whose
  // rows are all hidden (agent control off by policy) says so.
  if (!query && !paneHasContent(pane)) {
    return [h('h1', {}, pane.title),
            h('p', {class: 'empty'},
              'Your organization has turned these settings off.')];
  }
  if (pane.id === settings.shortcutsPane) {
    return renderShortcuts(query);
  }
  const groups = renderGroups(pane, query);
  return pane.id === 'agents' ?
      [...groups, ...renderAgentContainers(query)] : groups;
}

// Page ------------------------------------------------------------------------

// Returns how many settings match `query` across the panes (0 without one).
function render() {
  if (!root || !settings) {
    return 0;
  }
  updaters = [];
  const pane = root.getElementById('pane');
  let count = 0;
  if (query) {
    pane.replaceChildren(...settings.panes.flatMap(p => renderPane(p, query)));
    // The settings found, for Chromium's results count and "No results".
    count = pane.querySelectorAll('.row, .shortcut').length;
  } else {
    const current = settings.panes.find(p => p.id === currentPane);
    pane.replaceChildren(...(current ? renderPane(current) : []));
  }
  updateAll();
  return count;
}

// The pane a route shows: chrome://settings/koan/<pane>, and the first pane
// at chrome://settings/ (Koan's Look and Feel is the top-level section).
function paneForRoute(route, router) {
  if (route === router.getRoutes().BASIC) {
    return settings.panes[0]?.id;
  }
  if (!route.path.startsWith(PANE_PATH)) {
    return null;
  }
  const id = route.path.slice(PANE_PATH.length);
  return settings.panes.some(p => p.id === id) ? id : settings.panes[0]?.id;
}

function reloadLookTokens() {
  // The panes draw from the same tokens as the browser: fetch them again.
  const href = `koan_tokens.css?${Date.now()}`;
  for (const link of [document.getElementById('koan-look-tokens'),
                      root?.getElementById('look-tokens')]) {
    if (link) {
      link.href = href;
    }
  }
}

const ready = (async () => {
  settings = await sendWithPromise('koanGetSettings');
  // Not awaited: where agent control isn't built nothing answers.
  sendWithPromise('koanGetAgentContainers').then(containers => {
    agentContainers = containers;
    render();
  });
  addWebUiListener('koan-pref-changed', (pref, value) => {
    settings.prefs[pref] = value;
    updateAll();
    if (pref.startsWith('koan.look.')) {
      reloadLookTokens();
    }
  });
  addWebUiListener('koan-agent-containers-changed', containers => {
    agentContainers = containers;
    // Don't take the text out of a name being typed.
    if ((query || currentPane === 'agents') &&
        !root?.activeElement?.classList.contains('container-name')) {
      render();
    }
  });
  addWebUiListener('koan-shortcuts-changed', shortcuts => {
    settings.shortcuts = shortcuts;
    updateAll();
  });
})();

// The look's tokens for the whole page too: its fonts, and Chromium's
// sections in the look (koan_settings_ui.cc, LookStylesheet()).
document.head.append(h('link', {
  rel: 'stylesheet',
  id: 'koan-look-tokens',
  href: 'koan_tokens.css',
}));

// The menu items' icons (settings_menu.html hook: koan:<pane>), Zen's.
const SVG = 'http://www.w3.org/2000/svg';
const ICONS = {
  looks: {fill: 'currentColor', d: 'M4 1h8v6.5H9.3V14a1.3 1.3 0 0 1-2.6 0V7.5H4z'},
  tabs: {d: 'M3.1 2.6h9.8a1.6 1.6 0 0 1 1.6 1.6v7.6a1.6 1.6 0 0 1-1.6 1.6H3.1a1.6 1.6 0 0 1-1.6-1.6V4.2a1.6 1.6 0 0 1 1.6-1.6zM1.5 5.6h13'},
  shortcuts: {d: 'M2.7 3.6h10.6a1.6 1.6 0 0 1 1.6 1.6v5.6a1.6 1.6 0 0 1-1.6 1.6H2.7a1.6 1.6 0 0 1-1.6-1.6V5.2a1.6 1.6 0 0 1 1.6-1.6zM4 6.6h.5M7.2 6.6h.5M10.4 6.6h.5M5 9.6h6'},
  agents: {d: 'M4.6 5.1h6.8a2 2 0 0 1 2 2v4.3a2 2 0 0 1-2 2H4.6a2 2 0 0 1-2-2V7.1a2 2 0 0 1 2-2zM8 5.1V2.4M6 8.6v1.2M10 8.6v1.2M1 9h1.6M13.4 9H15'},
};
{
  const defs = document.createElementNS(SVG, 'defs');
  for (const [id, icon] of Object.entries(ICONS)) {
    const g = document.createElementNS(SVG, 'g');
    g.id = id;
    const path = document.createElementNS(SVG, 'path');
    path.setAttribute('d', icon.d);
    // Presentation attributes win over cr-icon's inherited fill.
    path.setAttribute('fill', icon.fill ?? 'none');
    path.setAttribute('stroke', icon.fill ? 'none' : 'currentColor');
    path.setAttribute('stroke-width', '1.25');
    path.setAttribute('stroke-linecap', 'round');
    path.setAttribute('stroke-linejoin', 'round');
    g.append(path);
    defs.append(g);
  }
  const svg = document.createElementNS(SVG, 'svg');
  svg.append(defs);
  // Not defined yet: the settings bundle's own cr-iconset upgrades it when it
  // loads, before the menu draws, and registers it where the menu's icons
  // look. Attributes, which the element reads when it upgrades.
  const iconset = document.createElement('cr-iconset');
  iconset.setAttribute('name', 'koan');
  iconset.setAttribute('size', '16');
  iconset.append(svg);
  document.head.append(iconset);
}

// <koan-settings-page> (settings_main.html hook): one plugin for every Koan
// section, showing the pane of the current route.
class KoanSettingsPageElement extends HTMLElement {
  constructor() {
    super();
    root = this.attachShadow({mode: 'open'});
    root.append(
        h('link', {rel: 'stylesheet', href: 'koan_settings.css'}),
        h('link', {rel: 'stylesheet', href: 'koan_tokens.css', id: 'look-tokens'}),
        h('div', {id: 'pane'}));
    // For the end-to-end tests, which can't reach into Chromium's shadow
    // roots by themselves.
    window.koanSettingsRoot = root;
  }

  async connectedCallback() {
    const {Router} = await settingsModule;
    await ready;
    if (!this.isConnected) {
      return;
    }
    this.router_ = Router.getInstance();
    this.router_.addObserver(this);
    this.currentRouteChanged(this.router_.getCurrentRoute());
  }

  disconnectedCallback() {
    this.router_?.removeObserver(this);
    this.router_ = null;
  }

  // RouteObserverMixinInterface.
  currentRouteChanged(route) {
    const pane = paneForRoute(route, this.router_);
    if (pane && pane !== currentPane) {
      currentPane = pane;
      if (!query) {
        render();
      }
    }
  }

  // SettingsPlugin: Chromium's search asks every section; this one filters
  // Koan's settings, across its panes.
  async searchContents(text) {
    await ready;
    query = text.trim().toLowerCase();
    const count = render();
    return {canceled: false, matchCount: count, wasClearSearch: !query};
  }
}

customElements.define('koan-settings-page', KoanSettingsPageElement);
