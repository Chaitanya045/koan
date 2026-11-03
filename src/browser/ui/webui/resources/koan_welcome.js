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

// chrome://koan-welcome: Zen's welcome flow (1.24b's ZenWelcome.mjs) as a
// page over the window. What Zen asks of Firefox (the migration wizard, the
// search service, the shell service, Essentials, the theme picker) goes to
// koan_welcome_handler.cc; Zen's motion library is the Web Animations API
// with Zen's durations, offsets and staggers.

import {sendWithPromise} from 'chrome://resources/js/cr.js';

// zen-welcome.ftl, with Koan's name for -brand-short-name.
const kStrings = {
  'zen-welcome-title-line1': 'Welcome to',
  'zen-welcome-title-line2': 'a calmer internet',
  'zen-welcome-back': 'Back',
  'zen-welcome-skip': 'Skip for now',
  'zen-generic-next': 'Next',
  'zen-welcome-import-title': 'Bring your stuff along.',
  'zen-welcome-import-description':
      'Pick a browser to import your bookmarks, history and passwords from ' +
      'and pick up right where you left off.',
  'zen-welcome-import-no': '<strong>NO</strong>, start fresh.',
  'zen-welcome-default-browser-title': 'Make Koan your default?',
  'zen-welcome-default-browser-description':
      'Links from other apps will open in Koan. You can always change this ' +
      'later.',
  'zen-welcome-set-default-browser': '<strong>YES</strong>, make Koan default.',
  'zen-welcome-dont-set-default-browser':
      '<strong>NO</strong>, still exploring my options.',
  'zen-welcome-default-search-title': 'Pick your search engine.',
  'zen-welcome-default-search-description':
      'Choose your default search engine. You can always change it later!',
  'zen-welcome-essentials-title': 'Choose the apps you use most.',
  'zen-welcome-essentials-description':
      'Select your favorite apps to keep them handy in your sidebar.',
  'zen-welcome-workspace-colors-title': 'Your space, your colors.',
  'zen-welcome-workspace-colors-description':
      'Personalize your browser by giving each space its own unique color ' +
      'identity.',
  'zen-welcome-start-browsing-title': 'All set? Let’s get rolling!',
  'zen-welcome-start-browsing-description-1':
      'You’re all set up and ready to go. Click the button below to start ' +
      'browsing with Koan.',
  'zen-welcome-start-browsing': 'Dive in!',
};

// Sets `element`'s text from `id`; Zen's strings mark "YES" / "NO" with
// <strong>, the only markup they use.
function setText(element, id) {
  const text = kStrings[id] ?? id;
  element.replaceChildren();
  for (const [i, part] of text.split(/<\/?strong>/).entries()) {
    if (!part) {
      continue;
    }
    if (i % 2) {
      const strong = document.createElement('strong');
      strong.textContent = part;
      element.appendChild(strong);
    } else {
      element.append(part);
    }
  }
}

const kEssentialApps = [
  {url: 'https://obsidian.md', icon: 'obsidian', color: '#7c3aed', name: 'Obsidian'},
  {url: 'https://discord.com', icon: 'discord', color: '#5865f2', name: 'Discord'},
  {url: 'https://trello.com', icon: 'trello', color: '#0052cc', name: 'Trello'},
  {url: 'https://slack.com', icon: 'slack', color: '#e01e5a', name: 'Slack'},
  {url: 'https://github.com', icon: 'github', color: '#57606a', name: 'GitHub'},
  {url: 'https://app.tuta.com/', icon: 'tuta', color: '#c00002', name: 'Tuta'},
  {url: 'https://notion.com', icon: 'notion', color: '#37352f', name: 'Notion'},
  {url: 'https://calendar.google.com', icon: 'calendar', color: '#4285f4', name: 'Calendar'},
  {url: 'https://figma.com', icon: 'figma', color: '#f24e1e', name: 'Figma'},
];

const gChoices = {
  setDefaultBrowser: false,
  essentials: new Set(),
};

// ---- Motion: Zen's motion.animate() on the Web Animations API.

const kReducedMotion = matchMedia('(prefers-reduced-motion: reduce)');

// Zen's springs, as an ease with a little overshoot; its "easeIn" and
// "easeOut".
const kEasings = {
  spring: 'cubic-bezier(0.34, 1.56, 0.64, 1)',
  easeIn: 'cubic-bezier(0.42, 0, 1, 1)',
  easeOut: 'cubic-bezier(0, 0, 0.58, 1)',
};

function stagger(step, {startDelay = 0} = {}) {
  return index => startDelay + step * index;
}

// animate(targets, {opacity: [from, to], x: [...], y: [...], filter: [...]},
// {duration | visualDuration (seconds), delay (seconds or stagger), type,
// ease}) returns a promise that settles when every target is done.
function animate(targets, values, options = {}) {
  const elements = typeof targets === 'string' ?
      [...document.querySelectorAll(targets)] :
      targets instanceof Element ? [targets] : [...targets];
  const seconds = options.visualDuration ?? options.duration ?? 0.3;
  const easing = options.type === 'spring' ?
      kEasings.spring :
      kEasings[options.ease] ?? kEasings.easeOut;
  const frames = [{}, {}];
  for (const [property, range] of Object.entries(values)) {
    const [from, to] = Array.isArray(range) ? range : [undefined, range];
    if (property === 'x' || property === 'y') {
      const axis = property === 'x' ? 'X' : 'Y';
      if (from !== undefined) {
        frames[0].translate = `${axis === 'X' ? from : 0}px ${axis === 'Y' ? from : 0}px`;
      }
      frames[1].translate = `${axis === 'X' ? to : 0}px ${axis === 'Y' ? to : 0}px`;
    } else {
      if (from !== undefined) {
        frames[0][property] = from;
      }
      frames[1][property] = to;
    }
  }
  if (!Object.keys(frames[0]).length) {
    frames.shift();
  }
  return Promise.all(elements.map((element, index) => {
    const delay = typeof options.delay === 'function' ?
        options.delay(index) :
        options.delay ?? 0;
    const animation = element.animate(frames, {
      duration: kReducedMotion.matches ? 0 : seconds * 1000,
      delay: kReducedMotion.matches ? 0 : delay * 1000,
      easing,
      fill: 'forwards',
    });
    return animation.finished.catch(() => {});
  }));
}

const kSpring = {type: 'spring', bounce: 0.35, visualDuration: 0.35};
const kExit = {duration: 0.12, ease: 'easeIn'};
const kFade = {duration: 0.25, ease: 'easeOut'};

// ---- Building blocks.

function createOption({id, group, checked, l10n, text, extra}) {
  const label = document.createElement('label');
  label.className = 'zen-welcome-option';
  label.setAttribute('for', id);
  const input = document.createElement('input');
  input.type = 'radio';
  input.id = id;
  input.name = group;
  input.checked = !!checked;
  label.appendChild(input);
  const radio = document.createElement('span');
  radio.className = 'zen-welcome-option-radio no-squircles';
  label.appendChild(radio);
  if (extra) {
    label.appendChild(extra);
  }
  const labelText = document.createElement('span');
  labelText.className = 'zen-welcome-option-label';
  if (l10n) {
    setText(labelText, l10n);
  } else {
    labelText.textContent = text;
  }
  label.appendChild(labelText);
  return label;
}

// An engine's or a browser's icon. Without one (Chromium bundles engine
// logos only in Google Chrome builds), the name's initial on a neutral tile.
function createIconWrapper(url, name = '') {
  const iconWrapper = document.createElement('div');
  iconWrapper.className = 'engine-icon-wrapper';
  if (url) {
    const iconBackdrop = document.createElement('img');
    iconBackdrop.className = 'engine-icon-backdrop';
    const icon = document.createElement('img');
    icon.className = 'engine-icon';
    icon.width = icon.height = 40;
    icon.src = iconBackdrop.src = url;
    iconWrapper.append(iconBackdrop, icon);
  } else {
    const letter = document.createElement('span');
    letter.className = 'engine-icon engine-icon-letter';
    letter.textContent = name.trim().slice(0, 1).toUpperCase();
    iconWrapper.appendChild(letter);
  }
  return iconWrapper;
}

// A browser's card on the import page, followed by its profiles to pick
// from when it has more than one.
function createImportSource(source, index) {
  const container = document.createElement('div');
  container.className = 'zen-welcome-import-source';
  const option = createOption({
    id: 'zen-welcome-import-' + index,
    group: 'zen-welcome-import',
    text: source.name,
    extra: createIconWrapper(source.image, source.name),
  });
  option.querySelector('input').dataset.sourceIndex = index;
  container.appendChild(option);
  if (source.profiles.length > 1) {
    const profiles = document.createElement('div');
    profiles.className = 'zen-welcome-import-profiles';
    const list = document.createElement('div');
    list.className = 'zen-welcome-import-profiles-list';
    source.profiles.forEach((profile, profileIndex) => {
      const row = document.createElement('label');
      row.className = 'zen-welcome-import-profile';
      const input = document.createElement('input');
      input.type = 'radio';
      input.name = `zen-welcome-import-profile-${index}`;
      input.value = profileIndex;
      input.checked = profileIndex === 0;
      const check = document.createElement('span');
      check.className = 'zen-welcome-import-profile-check no-squircles';
      const text = document.createElement('span');
      text.className = 'zen-welcome-import-profile-name';
      text.textContent = profile?.name || profile?.id || source.name;
      row.append(input, check, text);
      list.appendChild(row);
    });
    profiles.appendChild(list);
    container.appendChild(profiles);
  }
  return container;
}

function updateExpandedImportSource(content) {
  for (const source of content.querySelectorAll('.zen-welcome-import-source')) {
    source.toggleAttribute(
        'expanded',
        source.querySelector('input[name=\'zen-welcome-import\']').checked);
  }
}

// ---- The keyboard (Koan's): each list of choices is one Tab stop (radio
// groups natively; the Essentials grid by roving focus), Enter chooses like
// Space, and typing a letter jumps to the next choice starting with it.

function choiceName(element) {
  return (element.closest('label') ?? element).textContent.trim();
}

function jumpToLetter(items, from, key) {
  if (!/^[a-z]$/i.test(key) || !items.length) {
    return null;
  }
  const start = Math.max(0, items.indexOf(from));
  for (let step = 1; step <= items.length; ++step) {
    const item = items[(start + step) % items.length];
    if (choiceName(item).toLowerCase().startsWith(key.toLowerCase())) {
      return item;
    }
  }
  return null;
}

document.addEventListener('keydown', event => {
  if (event.key === 'Escape') {
    chrome.send('skip');
    return;
  }
  const target = event.target;
  if (event.ctrlKey || event.metaKey || event.altKey) {
    return;
  }
  if (target instanceof HTMLInputElement && target.type === 'radio') {
    const group = [...document.querySelectorAll(
        `input[type=radio][name="${target.name}"]`)];
    if (event.key === 'Enter') {
      target.click();
      event.preventDefault();
      return;
    }
    const match = jumpToLetter(group, target, event.key);
    if (match) {
      match.focus();
      match.click();
      event.preventDefault();
    }
    return;
  }
  if (target.classList?.contains('zen-welcome-essential')) {
    const tiles = [...document.querySelectorAll('.zen-welcome-essential')];
    const index = tiles.indexOf(target);
    const moves = {ArrowLeft: -1, ArrowRight: 1, ArrowUp: -3, ArrowDown: 3};
    let next = null;
    if (event.key in moves) {
      next = tiles[index + moves[event.key]] ?? null;
    } else {
      next = jumpToLetter(tiles, target, event.key);
    }
    if (next) {
      for (const tile of tiles) {
        tile.tabIndex = tile === next ? 0 : -1;
      }
      next.focus();
      event.preventDefault();
    }
  }
});

// ---- The pages.

class KoanWelcomePages {
  #index = -1;
  #pages;
  #content = null;
  #renderToken = 0;
  #buttonsToken = 0;

  constructor(pages) {
    this.#pages = pages;
    this.init();
    this.next();
  }

  get textContainer() {
    return document.getElementById('zen-welcome-page-sidebar-content');
  }

  get contentContainer() {
    return document.getElementById('zen-welcome-page-content');
  }

  get buttonsContainer() {
    return document.getElementById('zen-welcome-page-sidebar-buttons');
  }

  get backButton() {
    return document.getElementById('zen-welcome-back');
  }

  get currentPage() {
    return this.#pages[this.#index];
  }

  get content() {
    return this.#content;
  }

  init() {
    document.getElementById('zen-welcome-start').remove();
    const pages = document.getElementById('zen-welcome-pages');
    pages.style.display = 'flex';
    animate(pages, {opacity: [0, 1]}, {...kFade, duration: 0.45});
    this.backButton.addEventListener('click', () => this.back());
  }

  next() {
    this.currentPage?.commit?.(this.#content);
    this.#show(this.#index + 1, 1);
  }

  back() {
    if (this.#index <= 0) {
      return;
    }
    this.#show(this.#index - 1, -1);
  }

  #show(index, direction) {
    const previous = this.currentPage;
    while (this.#pages[index]?.skip?.()) {
      index += direction;
    }
    if (index < 0) {
      return;
    }
    this.#index = index;
    const page = this.currentPage;
    if (!page) {
      this.finish();
      return;
    }
    previous?.leave?.();
    this.#exit(this.textContainer, {x: [0, -80 * direction]});
    this.#exit(this.contentContainer, {});
    this.backButton.toggleAttribute('disabled', index === 0);
    this.updateButtons();
    this.#renderText(page, direction);
    this.#renderContent(page);
  }

  #exit(container, keyframes) {
    for (const element of container.children) {
      if (element.hasAttribute('exiting')) {
        continue;
      }
      element.setAttribute('exiting', '');
      animate(element, {opacity: [1, 0], ...keyframes}, kExit)
          .then(() => element.remove());
    }
  }

  #renderText(page, direction) {
    ++this.#renderToken;
    const text = document.createElement('div');
    text.className = 'zen-welcome-text';
    const title = document.createElement('h1');
    setText(title, page.title);
    text.appendChild(title);
    for (const id of page.descriptions ?? []) {
      const p = document.createElement('p');
      setText(p, id);
      text.appendChild(p);
    }
    this.textContainer.appendChild(text);
    animate([...text.children], {opacity: [0, 1], x: [120 * direction, 0]}, {
      ...kSpring,
      bounce: 0.4,
      visualDuration: 0.3,
      delay: stagger(0.03),
    });
  }

  #renderContent(page) {
    const content = document.createElement('div');
    content.className = 'zen-welcome-page';
    if (page.id) {
      content.setAttribute('page', page.id);
    }
    page.render(content, this);
    this.contentContainer.appendChild(content);
    this.#content = content;
    animate(content, {opacity: [0, 1]}, kFade);
  }

  updateButtons() {
    ++this.#buttonsToken;
    const fragment = document.createDocumentFragment();
    for (const button of this.currentPage.buttons) {
      const element = document.createElement('button');
      element.className = button.primary ? 'zen-big-accent-button primary' :
                                           'zen-big-accent-button';
      setText(element, button.l10n);
      element.addEventListener('click', () => {
        if (button.onclick?.(this) !== false) {
          this.next();
        }
      });
      fragment.appendChild(element);
    }
    this.buttonsContainer.replaceChildren(fragment);
    // The keyboard starts on the page's choices, else on its first button.
    requestAnimationFrame(() => {
      const first = this.#content?.querySelector(
                        'input[type=radio]:checked, .zen-welcome-essential, ' +
                        '.zen-welcome-color') ??
          this.buttonsContainer.firstElementChild;
      first?.focus({preventScroll: true});
    });
  }

  async finish() {
    ++this.#buttonsToken;
    this.buttonsContainer.replaceChildren();
    this.backButton.toggleAttribute('disabled', true);
    await animate('#zen-welcome', {opacity: [1, 0]}, {duration: 0.3});
    // Koan closes the screen and pins the Essentials (KoanWelcome).
    chrome.send('finish', [[...gChoices.essentials]]);
  }
}

// ---- Zen's pages, without the ad blocker page: Zen skips it when it can't
// offer uBlock Origin, and Koan, a Chromium, can't install it for the user.

const kNextButton = {l10n: 'zen-generic-next', primary: true};

function getWelcomePages(data) {
  return [
    {
      id: 'import',
      title: 'zen-welcome-import-title',
      descriptions: ['zen-welcome-import-description'],
      buttons: [kNextButton],
      render(content) {
        content.appendChild(createOption({
          id: 'zen-welcome-import-no',
          group: 'zen-welcome-import',
          l10n: 'zen-welcome-import-no',
          checked: true,
        }));
        content.importSources = data.importSources;
        data.importSources.forEach((source, index) => {
          content.appendChild(createImportSource(source, index));
        });
        content.addEventListener('change', event => {
          if (event.target.name === 'zen-welcome-import') {
            updateExpandedImportSource(content);
          }
        });
        updateExpandedImportSource(content);
      },
      commit(content) {
        const checked =
            content.querySelector('input[name=\'zen-welcome-import\']:checked');
        const source = content.importSources?.[checked?.dataset.sourceIndex];
        if (!source) {
          return;
        }
        const profileIndex =
            checked.closest('.zen-welcome-import-source')
                .querySelector('input[type=\'radio\'][value]:checked')
                ?.value ??
            0;
        chrome.send('import', [source.key, Number(profileIndex)]);
      },
    },
    {
      id: 'colors',
      title: 'zen-welcome-workspace-colors-title',
      descriptions: ['zen-welcome-workspace-colors-description'],
      buttons: [kNextButton],
      // Zen opens its gradient theme picker here; until Koan has it (#41),
      // a panel of its colours in the picker's place.
      render(content) {
        const panel = document.createElement('div');
        panel.className = 'zen-welcome-colors-panel';
        panel.setAttribute('role', 'radiogroup');
        panel.setAttribute('aria-label', 'Colors');
        for (const color of data.colors) {
          const label = document.createElement('label');
          label.className = 'zen-welcome-color';
          label.title = color.name;
          label.style.setProperty('--zen-welcome-swatch', color.value);
          const input = document.createElement('input');
          input.type = 'radio';
          input.name = 'zen-welcome-color';
          input.value = color.value;
          input.setAttribute('aria-label', color.name);
          const name = document.createElement('span');
          name.className = 'zen-welcome-color-name';
          name.textContent = color.name;
          label.append(input, name);
          input.addEventListener(
              'change', () => chrome.send('setColor', [color.value]));
          panel.appendChild(label);
        }
        content.appendChild(panel);
      },
    },
    {
      id: 'search',
      title: 'zen-welcome-default-search-title',
      descriptions: ['zen-welcome-default-search-description'],
      buttons: [kNextButton],
      render(content) {
        for (const engine of data.engines) {
          const option = createOption({
            id: 'zen-welcome-engine-' + engine.id,
            group: 'zen-welcome-search-engine',
            text: engine.name,
            checked: engine.id === data.defaultEngine,
            extra: createIconWrapper(engine.icon, engine.name),
          });
          option.querySelector('input').addEventListener(
              'change', () => chrome.send('setSearchEngine', [engine.id]));
          content.appendChild(option);
        }
      },
    },
    {
      id: 'essentials',
      title: 'zen-welcome-essentials-title',
      descriptions: ['zen-welcome-essentials-description'],
      buttons: [
        kNextButton,
        {
          l10n: 'zen-welcome-skip',
          onclick(pages) {
            for (const button of pages.content.querySelectorAll(
                     '.zen-welcome-essential[selected]')) {
              button.removeAttribute('selected');
              button.setAttribute('aria-pressed', 'false');
            }
          },
        },
      ],
      render(content) {
        content.innerHTML = `
          <div class="zen-welcome-mock-browser">
            <div class="zen-welcome-mock-browser-sidebar">
              <div class="zen-welcome-mock-browser-dots">
                <div class="no-squircles"></div>
                <div class="no-squircles"></div>
                <div class="no-squircles"></div>
              </div>
              <div id="zen-welcome-essentials"></div>
              <div class="zen-welcome-mock-tab"></div>
              <div class="zen-welcome-mock-tab"></div>
            </div>
          </div>`;
        const grid = content.querySelector('#zen-welcome-essentials');
        kEssentialApps.forEach((app, index) => {
          const button = document.createElement('button');
          button.className = 'zen-welcome-essential';
          button.dataset.url = app.url;
          button.title = app.name;
          button.setAttribute('aria-label', app.name);
          button.tabIndex = index === 0 ? 0 : -1;
          button.style.setProperty(
              '--zen-welcome-app-icon', `url("/favicons/${app.icon}.svg")`);
          button.style.setProperty('--zen-welcome-app-color', app.color);
          const selected = gChoices.essentials.has(app.url);
          button.toggleAttribute('selected', selected);
          button.setAttribute('aria-pressed', String(selected));
          button.addEventListener('click', () => {
            button.toggleAttribute('selected');
            button.setAttribute(
                'aria-pressed', String(button.hasAttribute('selected')));
          });
          grid.appendChild(button);
        });
      },
      commit(content) {
        gChoices.essentials = new Set(
            [...content.querySelectorAll('.zen-welcome-essential[selected]')]
                .map(button => button.dataset.url));
      },
    },
    {
      id: 'default-browser',
      title: 'zen-welcome-default-browser-title',
      descriptions: ['zen-welcome-default-browser-description'],
      buttons: [kNextButton],
      render(content) {
        content.appendChild(createOption({
          id: 'zen-welcome-set-default-browser',
          group: 'zen-welcome-default-browser',
          l10n: 'zen-welcome-set-default-browser',
          checked: gChoices.setDefaultBrowser,
        }));
        content.appendChild(createOption({
          id: 'zen-welcome-dont-set-default-browser',
          group: 'zen-welcome-default-browser',
          l10n: 'zen-welcome-dont-set-default-browser',
          checked: !gChoices.setDefaultBrowser,
        }));
      },
      commit(content) {
        gChoices.setDefaultBrowser =
            content.querySelector('#zen-welcome-set-default-browser').checked;
        if (gChoices.setDefaultBrowser) {
          chrome.send('setDefaultBrowser');
        }
      },
    },
    {
      id: 'finish',
      title: 'zen-welcome-start-browsing-title',
      descriptions: ['zen-welcome-start-browsing-description-1'],
      buttons: [{l10n: 'zen-welcome-start-browsing', primary: true}],
      render() {},
    },
  ];
}

// ---- The first screen: the title spelled out, then the arrow button.

async function animateInitialStage(data) {
  const titleElement = document.getElementById('zen-welcome-title');
  for (const id of ['zen-welcome-title-line1', 'zen-welcome-title-line2']) {
    const lineElement = document.createElement('span');
    for (const char of kStrings[id]) {
      if (char === ' ') {
        lineElement.append(' ');
        continue;
      }
      const charElement = document.createElement('span');
      charElement.className = 'zen-welcome-char';
      charElement.textContent = char;
      lineElement.appendChild(charElement);
    }
    titleElement.appendChild(lineElement);
  }
  titleElement.setAttribute(
      'aria-label',
      `${kStrings['zen-welcome-title-line1']} ${
          kStrings['zen-welcome-title-line2']}`);
  const chars = titleElement.querySelectorAll('.zen-welcome-char');
  const button = document.getElementById('zen-welcome-start-button');
  button.focus({preventScroll: true});
  button.addEventListener('click', async () => {
    await animate(
        '#zen-welcome-title .zen-welcome-char, #zen-welcome-start-button',
        {opacity: [1, 0], y: [0, -14]},
        {duration: 0.3, ease: 'easeIn', delay: stagger(0.012)});
    new KoanWelcomePages(getWelcomePages(data));
  }, {once: true});
  await animate(chars, {opacity: [0, 1], y: [50, 0]}, {
    delay: stagger(0.035, {startDelay: 0.8}),
    type: 'spring',
    bounce: 0.3,
    visualDuration: 0.45,
  });
  await animate(
      button, {opacity: [0, 1], y: [20, 0], filter: ['blur(2px)', 'blur(0px)']},
      {delay: 0.1, type: 'spring', visualDuration: 0.5});
}

async function start() {
  setText(document.getElementById('zen-welcome-back'), 'zen-welcome-back');
  const data = await sendWithPromise('getWelcomeData');
  animateInitialStage(data);
}

start();
