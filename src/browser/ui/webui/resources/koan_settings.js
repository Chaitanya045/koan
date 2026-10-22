// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material ported from Zen Browser, Copyright the Zen Browser
// contributors (https://github.com/zen-browser/desktop), MPL-2.0:
//   zen-preferences.ftl (strings)
// Koan's changes, Copyright 2026 The Koan Authors, are under the same
// licence.

// chrome://koan-settings, laid out like Zen's preferences. Everything shown
// comes from the browser (koan_settings_rows.cc and koan_shortcut_table.cc);
// add settings there, not here.

import {addWebUiListener, sendWithPromise} from 'chrome://resources/js/cr.js';

let settings;
let currentPane;
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
  if (!await sendWithPromise('setPref', row.pref, value)) {
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
      el = h('div', {class: 'row menu'}, h('span', {}, row.label), select);
      updaters.push(() => {
        select.selectedIndex =
            row.options.findIndex(o => o.value === prefValue(row.pref));
        select.disabled = disabled();
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

// Zen's display: Ctrl+Win+Alt+⇧+Key (also shortcuts::DisplayText() in
// koan_shortcut_table.cc, for the command palette).
// TODO(#51): on macOS Zen shows ⌃ ⌥ ⇧ ⌘ separated by spaces, ⌘ for "accel",
// and ⎋ / ↩ for Escape / Enter; record ⌘ (metaKey) as the accelerator.
function modifiersText(modifiers) {
  const m = settings.shortcuts.modifiers;
  return [[m.ctrl, 'Ctrl'], [m.meta, 'Win'], [m.alt, 'Alt'], [m.shift, '⇧']]
      .filter(([flag]) => modifiers & flag)
      .map(([, name]) => `${name}+`)
      .join('');
}

function keyName(code) {
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

// Like the browser's shortcut_table: a key with Ctrl, Alt or Win, or a
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
  chrome.send('setEditing', [true]);
}

function onShortcutBlur(input, action) {
  chrome.send('setEditing', [false]);
  input.classList.remove('editing');
  if (!editor.saved) {
    input.classList.add('unsaved');
    setNote(input, 'unsaved', 'Unsaved shortcut! Please save it by ' +
            'clicking the "Escape" key after retyping it.');
  }
  // The field shows the key that is bound; a refused one stays explained by
  // its note.
  showKeys(input, action);
  editor.action = null;
  editor.latest = null;
  editor.saved = true;
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
  if (!modifiers && event.key === 'Escape') {
    const key = editor.latest;
    if (key) {
      const conflict = conflictOf(key, action);
      let problem = null;
      if (conflict) {
        problem =
            `Conflicts with ${groupTitle(conflict.group)} -> ${conflict.label}`;
      } else if (isTabCycling(key)) {
        problem = 'Ctrl+Tab and Ctrl+Page Up/Down switch tabs';
      } else if (!isValidKey(key)) {
        problem = 'Use Ctrl, Alt or Win with the key, or a function key';
      }
      if (problem) {
        input.classList.add('invalid');
        setNote(input, 'conflict', problem);
      } else if (await sendWithPromise('setShortcut', action.id, key)) {
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
    editor.saved = true;
    input.blur();
    return;
  }
  if (!modifiers && event.key === 'Backspace') {
    editor.latest = null;
    editor.saved = true;
    input.classList.remove('invalid');
    input.value = 'Not set';
    input.classList.add('not-set');
    setNote(input, null);
    sendWithPromise('setShortcut', action.id, null);
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
            sendWithPromise('resetShortcuts');
          }
        },
      }, 'Reset to Default'),
      ...groups),
  ];
}

// Page ------------------------------------------------------------------------

function render() {
  updaters = [];
  const pane = document.getElementById('pane');
  const query = document.getElementById('search').value.trim().toLowerCase();
  if (query) {
    const results = settings.panes.flatMap(
        p => p.id === settings.shortcutsPane ? renderShortcuts(query) :
                                                 renderGroups(p, query));
    pane.replaceChildren(
        h('h1', {}, 'Search Results'),
        ...(results.length ? results : [h('p', {class: 'empty'},
            `Sorry! There are no results in Settings for “${query}”.`)]));
  } else {
    const current = settings.panes.find(p => p.id === currentPane);
    pane.replaceChildren(...(current.id === settings.shortcutsPane ?
                                 renderShortcuts() : renderGroups(current)));
  }
  for (const button of document.querySelectorAll('#categories .category')) {
    button.setAttribute('aria-selected',
                        String(!query && button.dataset.pane === currentPane));
  }
  updateAll();
}

function selectPane(id) {
  currentPane = id;
  document.getElementById('search').value = '';
  history.replaceState(null, '', `/${id}`);
  render();
}

async function main() {
  settings = await sendWithPromise('getSettings');
  const panes = settings.panes.filter(paneHasContent);
  settings.panes = panes;
  document.getElementById('categories').replaceChildren(...panes.map(
      pane => h('button', {
        class: 'category',
        role: 'tab',
        id: `category-${pane.id}`,
        dataset: {pane: pane.id},
        onclick: () => selectPane(pane.id),
      }, h('span', {class: `icon icon-${pane.id}`}), pane.title)));
  const requested = location.pathname.slice(1);
  currentPane = panes.some(p => p.id === requested) ? requested : panes[0].id;
  document.getElementById('search').addEventListener('input', render);

  addWebUiListener('pref-changed', (pref, value) => {
    settings.prefs[pref] = value;
    updateAll();
  });
  addWebUiListener('shortcuts-changed', shortcuts => {
    settings.shortcuts = shortcuts;
    updateAll();
  });
  render();
}

main();
