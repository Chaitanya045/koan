// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// chrome://koan-welcome: Koan's optional first-run setup (onboarding v2,
// #181) in shadcn's look. Welcome, shortcuts, import, search engine, done;
// every step can be skipped, and nothing changes until its Next. The page
// builds its DOM without innerHTML (WebUI pages enforce Trusted Types).

import {sendWithPromise} from 'chrome://resources/js/cr.js';

// ---- DOM helpers.

function el(tag, attributes = {}, ...children) {
  const element = document.createElement(tag);
  for (const [name, value] of Object.entries(attributes)) {
    if (name === 'className') {
      element.className = value;
    } else if (value === true) {
      element.setAttribute(name, '');
    } else if (value !== false && value !== undefined && value !== null) {
      element.setAttribute(name, String(value));
    }
  }
  element.append(...children.filter(child => child !== null));
  return element;
}

function icon(name) {
  return el('span', {className: 'icon', 'data-icon': name, 'aria-hidden': true});
}

function button(label, {primary = false, outline = false, iconBefore, iconAfter,
                        onClick}) {
  const element = el(
      'button', {
        type: 'button',
        className:
            `button ${primary ? 'primary' : outline ? 'outline' : 'ghost'}`,
      },
      iconBefore ? icon(iconBefore) : null, el('span', {}, label),
      iconAfter ? icon(iconAfter) : null);
  element.addEventListener('click', onClick);
  return element;
}

// A radio card: [radio] [mark] title / detail [badge].
function choice({group, value, checked, title, detail, mark, badge}) {
  const input = el('input', {type: 'radio', name: group, value});
  input.checked = !!checked;
  return el(
      'label', {className: 'choice'}, input,
      el('span', {className: 'radio', 'aria-hidden': true}), mark ?? null,
      el('span', {className: 'choice-text'},
         el('span', {className: 'choice-title'}, title),
         detail ? el('span', {className: 'choice-detail'}, detail) : null),
      badge ? el('span', {className: 'badge'}, badge) : null);
}

// A name's initial on a muted tile.
function initialTile(name) {
  return el('span', {className: 'tile', 'aria-hidden': true},
            (name.trim()[0] ?? '?').toUpperCase());
}

// ---- The steps.

// What the user picked, kept across Back and Next.
const gChoices = {
  shortcuts: {},  // row id -> 'zen' | 'chrome'
  importSource: 'none',
  engine: null,
};

function shortcutsStep(data) {
  const preset = () => {
    const values = Object.values(gChoices.shortcuts);
    if (values.every(value => value === 'zen')) {
      return 'zen';
    }
    return values.every(value => value === 'chrome') ? 'chrome' : 'custom';
  };
  return {
    id: 'shortcuts',
    icon: 'keyboard',
    title: 'Pick your shortcuts',
    description:
        'Koan uses Zen\'s keys, which give a few of Chrome\'s keys a new ' +
        'job. Keep them, or keep Chrome\'s where they differ.',
    skip: () => !data.shortcutRows.length,
    render(body) {
      const presets = el(
          'div', {className: 'choice-list', role: 'radiogroup',
                  'aria-label': 'Shortcut set'},
          choice({
            group: 'shortcut-preset',
            value: 'zen',
            checked: preset() === 'zen',
            title: 'Zen\'s shortcuts',
            detail: 'Compact mode on ⌘S, Spaces on ⌥⌘← →.',
            badge: 'Recommended',
          }),
          choice({
            group: 'shortcut-preset',
            value: 'chrome',
            checked: preset() === 'chrome',
            title: 'Chrome\'s shortcuts',
            detail: 'Save page on ⌘S, tabs on ⌥⌘← →.',
          }));
      const rows = el('div', {className: 'key-rows'});
      for (const row of data.shortcutRows) {
        const segments = el('div', {
          className: 'segments',
          role: 'radiogroup',
          'aria-label': row.title,
        });
        for (const side of ['zen', 'chrome']) {
          const input = el('input', {
            type: 'radio',
            name: `shortcut-${row.id}`,
            value: side,
            'aria-label': `${row.title}: ${row[side].label} (${row[side].keys})`,
          });
          input.checked = gChoices.shortcuts[row.id] === side;
          input.addEventListener('change', () => {
            gChoices.shortcuts[row.id] = side;
            for (const presetInput of presets.querySelectorAll('input')) {
              presetInput.checked = presetInput.value === preset();
            }
          });
          segments.appendChild(el(
              'label', {className: 'segment'}, input,
              el('kbd', {}, row[side].keys),
              el('span', {}, row[side].label)));
        }
        rows.appendChild(el('div', {className: 'key-row'},
                            el('span', {className: 'key-row-title'}, row.title),
                            segments));
      }
      presets.addEventListener('change', event => {
        for (const row of data.shortcutRows) {
          gChoices.shortcuts[row.id] = event.target.value;
          rows.querySelector(
                  `input[name='shortcut-${row.id}'][value='${
                      event.target.value}']`)
              .checked = true;
        }
      });
      body.append(
          presets, rows,
          el('p', {className: 'step-note'},
             'Change any shortcut later in Settings › Keyboard Shortcuts.'));
    },
    commit() {
      chrome.send('setShortcuts', [{...gChoices.shortcuts}]);
    },
  };
}

function importStep(data) {
  return {
    id: 'import',
    icon: 'hard-drive-download',
    title: 'Bring your stuff along',
    description:
        'Import bookmarks, history and passwords from another browser. ' +
        'You choose what to bring in the next window.',
    render(body) {
      const list = el('div', {
        className: 'choice-list',
        role: 'radiogroup',
        'aria-label': 'Import from',
      });
      list.appendChild(choice({
        group: 'import',
        value: 'none',
        checked: gChoices.importSource === 'none',
        title: 'Start fresh',
        detail: 'Don\'t import anything.',
        mark: el('span', {className: 'tile', 'aria-hidden': true},
                 icon('sparkles')),
      }));
      // Only what Chromium's importer found (on macOS Safari, Firefox and a
      // bookmarks file); with nothing found, the import dialog itself.
      data.importSources.forEach((source, index) => {
        list.appendChild(choice({
          group: 'import',
          value: String(index),
          checked: gChoices.importSource === String(index),
          title: source.name,
          detail: source.profile || null,
          mark: initialTile(source.name),
        }));
      });
      if (!data.importSources.length) {
        list.appendChild(choice({
          group: 'import',
          value: 'dialog',
          checked: gChoices.importSource === 'dialog',
          title: 'Import from another browser…',
          detail: 'Opens the import window after setup.',
          mark: el('span', {className: 'tile', 'aria-hidden': true},
                   icon('hard-drive-download')),
        }));
      }
      list.addEventListener('change', event => {
        gChoices.importSource = event.target.value;
      });
      body.appendChild(list);
    },
    // Skip: nothing to import, whatever an earlier Next said.
    onSkip() {
      gChoices.importSource = 'none';
      chrome.send('import', [false]);
    },
    // Every Next says what is picked: a browser, then Back and "Start fresh",
    // imports nothing.
    commit() {
      chrome.send('import', [gChoices.importSource !== 'none']);
    },
  };
}

function searchStep(data) {
  return {
    id: 'search',
    icon: 'search',
    title: 'Choose your search engine',
    description: data.choiceScreen ?
        'Pick the engine that answers what you type in the address bar. ' +
            'You can change it later in Settings.' :
        'It answers what you type in the address bar. You can change it ' +
            'later in Settings.',
    // Skipped when a policy sets the engine (no engines then).
    skip: () => !data.engines.length,
    render(body) {
      const list = el('div', {
        className: 'choice-list',
        role: 'radiogroup',
        'aria-label': 'Search engine',
      });
      for (const engine of data.engines) {
        list.appendChild(choice({
          group: 'engine',
          value: String(engine.id),
          checked: String(engine.id) === String(gChoices.engine),
          title: engine.name,
          mark: initialTile(engine.name),
        }));
      }
      list.addEventListener('change', event => {
        gChoices.engine = Number(event.target.value);
      });
      body.appendChild(list);
    },
    // Set on Next. In a choice screen region (the EEA) the list is
    // Chromium's, in its order, with nothing preselected, and Next records
    // the choice (Chromium's own dialog then never shows); elsewhere Brave
    // is preselected.
    commit() {
      if (gChoices.engine !== null) {
        chrome.send('setSearchEngine', [gChoices.engine]);
      }
    },
  };
}

function getSteps(data) {
  return [
    {
      id: 'welcome',
      icon: 'sparkles',
      title: 'Welcome to a calmer internet',
      description: 'Set Koan up in a minute. Every step is optional, and ' +
          'you can change all of it later in Settings.',
    },
    shortcutsStep(data),
    importStep(data),
    searchStep(data),
    {
      id: 'done',
      icon: 'circle-check-big',
      title: 'You\'re all set',
      description: 'Enjoy the calm.',
    },
  ];
}

// ---- The flow.

class Welcome {
  #steps;
  #index = 0;
  constructor(steps) {
    this.#steps = steps;
    const list = document.getElementById('welcome-steps');
    for (const step of steps) {
      list.appendChild(el('li', {'data-step': step.id, 'aria-label': step.title}));
    }
    this.#render();
  }

  get #step() {
    return this.#steps[this.#index];
  }

  next() {
    this.#step.commit?.();
    this.#go(1);
  }

  back() {
    this.#go(-1);
  }

  // Leaves the setup: what Next already applied stays. On the last step it
  // is the same as Start browsing (a chosen import opens).
  skip() {
    endSetup(this.#index === this.#steps.length - 1 ? 'finish' : 'skip');
  }

  finish() {
    endSetup('finish');
  }

  // A step's own Skip: nothing of it applies.
  skipStep() {
    this.#step.onSkip?.();
    this.#go(1);
  }

  #go(direction) {
    let index = this.#index + direction;
    while (this.#steps[index]?.skip?.()) {
      index += direction;
    }
    if (index < 0 || index >= this.#steps.length) {
      return;
    }
    this.#index = index;
    this.#render();
  }

  #render() {
    const step = this.#step;
    const last = this.#index === this.#steps.length - 1;
    for (const [i, item] of
         [...document.querySelectorAll('#welcome-steps li')].entries()) {
      item.toggleAttribute('current', i === this.#index);
      item.toggleAttribute('done', i < this.#index);
      item.hidden = !!this.#steps[i].skip?.();
    }
    document.getElementById('welcome-skip').hidden = last;

    const card = document.getElementById('welcome-card');
    card.setAttribute('step', step.id);
    card.removeAttribute('entering');
    // (Restarts the entry animation.)
    void card.offsetWidth;
    card.setAttribute('entering', '');

    const head = el('div', {className: 'step-head'},
                    el('div', {className: 'step-badge'}, icon(step.icon)),
                    el('h1', {id: 'welcome-title'}, step.title),
                    el('p', {}, step.description));
    const body = el('div', {className: 'step-body'});
    step.render?.(body);
    const foot = el('div', {className: 'step-foot'});
    if (step.id === 'welcome') {
      foot.append(
          button('Get started', {
            primary: true,
            iconAfter: 'arrow-right',
            onClick: () => this.next(),
          }),
          button('Skip setup', {onClick: () => this.skip()}));
    } else if (last) {
      foot.append(button('Start browsing', {
        primary: true,
        iconAfter: 'arrow-right',
        onClick: () => this.finish(),
      }));
    } else {
      foot.append(
          button('Back', {iconBefore: 'arrow-left', onClick: () => this.back()}),
          el('span', {className: 'spacer'}),
          button('Skip', {onClick: () => this.skipStep()}),
          button('Next', {
            primary: true,
            iconAfter: 'arrow-right',
            onClick: () => this.next(),
          }));
    }
    card.replaceChildren(head, ...(body.childElementCount ? [body] : []), foot);
    card.setAttribute('aria-labelledby', 'welcome-title');
    // The keyboard starts on the step's choice, else its main button.
    (card.querySelector('input:checked') ??
     card.querySelector('.step-foot .button.primary'))
        ?.focus({preventScroll: true});
  }
}

// ---- The window around the page.

// Browser chrome, not a page: no Back / Reload / Inspect menu.
document.addEventListener('contextmenu', event => event.preventDefault());

// Ends the setup, once (a double click or a held Enter doesn't send twice).
let gEnded = false;
function endSetup(message) {
  if (gEnded) {
    return;
  }
  gEnded = true;
  chrome.send(message);
}

// Skip setup and Esc work from the first frame, before the page's data.
let gWelcome = null;
function skipSetup() {
  if (gWelcome) {
    gWelcome.skip();
  } else {
    endSetup('skip');
  }
}
document.getElementById('welcome-skip').addEventListener('click', skipSetup);
document.addEventListener('keydown', event => {
  if (event.key === 'Escape') {
    event.preventDefault();
    skipSetup();
  }
});
document.getElementById('welcome').hidden = false;

async function start() {
  const data = await sendWithPromise('getWelcomeData');
  if (!data) {
    // Not the first-run screen (the page typed into a tab): nothing to do.
    document.body.replaceChildren();
    return;
  }
  for (const row of data.shortcutRows) {
    gChoices.shortcuts[row.id] = row.chosen;
  }
  gChoices.engine = data.defaultEngine;
  gWelcome = new Welcome(getSteps(data));
}

start();
