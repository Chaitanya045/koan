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

// chrome://koan-welcome: Zen's welcome flow (1.24b's ZenWelcome.mjs), with
// Zen's own look, as a page over the window. What Zen asks of Firefox (the
// migration wizard, the search service, the shell service, Essentials, the
// theme picker) goes to koan_welcome_handler.cc; Zen's motion library is the
// Web Animations API with Zen's durations, offsets and staggers.

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

// ---- Motion: Zen's motion library (zen-vendor/motion.min.mjs, a UMD
// bundle loaded before this module), so the springs are Zen's own. With
// reduced motion everything lands at once.

const kReducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
const {animate: motionAnimate, stagger} = globalThis.Motion;

function animate(targets, keyframes, options = {}) {
  return motionAnimate(
      targets, keyframes, kReducedMotion.matches ? {duration: 0} : options);
}

// Zen's background video (drawn by koan_welcome.css), faded out when the
// colours page shows, as Zen does.
function removeVideoBackground() {
  const video = document.getElementById('zen-welcome-video');
  if (!video) {
    return;
  }
  animate(video, {opacity: 0}, {duration: 0.6, ease: 'easeOut'})
      .then(() => video.remove());
}

const kSpring = {type: 'spring', bounce: 0.35, visualDuration: 0.35};
const kExit = {duration: 0.12, ease: 'easeIn'};
const kFade = {duration: 0.25, ease: 'easeOut'};

// ---- Zen's accent: --zen-primary-color, from the system's accent the way
// ZenGradientGenerator.mjs works it out for a Space with the default theme
// (getNativeAccentColor, then getAccentColorForUI).

function blendColors(rgb1, rgb2, percentage) {
  const p = percentage / 100;
  return rgb1.map((c, i) => Math.round(c * p + rgb2[i] * (1 - p)));
}

function rgbToHsl(r, g, b) {
  r /= 255;
  g /= 255;
  b /= 255;
  const max = Math.max(r, g, b);
  const min = Math.min(r, g, b);
  const l = (max + min) / 2;
  if (max === min) {
    return [0, 0, l];
  }
  const d = max - min;
  const s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
  let h;
  if (max === r) {
    h = (g - b) / d + (g < b ? 6 : 0);
  } else if (max === g) {
    h = (b - r) / d + 2;
  } else {
    h = (r - g) / d + 4;
  }
  return [h * 60, s, l];
}

function hslToRgb(h, s, l) {
  if (s === 0) {
    const v = Math.round(l * 255);
    return [v, v, v];
  }
  const hue = (p, q, t) => {
    t = (t + 1) % 1;
    if (t < 1 / 6) {
      return p + (q - p) * 6 * t;
    }
    if (t < 1 / 2) {
      return q;
    }
    if (t < 2 / 3) {
      return p + (q - p) * (2 / 3 - t) * 6;
    }
    return p;
  };
  const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
  const p = 2 * l - q;
  return [hue(p, q, h + 1 / 3), hue(p, q, h), hue(p, q, h - 1 / 3)].map(
      v => Math.round(v * 255));
}

// The CSS AccentColor, as Zen reads it (InspectorUtils.colorToRGBA).
function cssAccentColor() {
  const probe = document.createElement('span');
  probe.style.color = 'AccentColor';
  document.body.appendChild(probe);
  const match = getComputedStyle(probe).color.match(/\d+(\.\d+)?/g);
  probe.remove();
  return match ? match.slice(0, 3).map(Number) : [0, 0, 0];
}

function zenPrimaryColor(systemAccent) {
  const dark = matchMedia('(prefers-color-scheme: dark)').matches;
  // getToolbarModifiedBaseRaw()
  const toolbarBase = dark ? [23, 23, 26] : [240, 240, 244];
  let rgb = blendColors(systemAccent, toolbarBase, dark ? 80 : 50);
  if (dark) {
    rgb = blendColors(rgb, [0, 0, 0], 40);
    return `rgb(${rgb.join(', ')})`;
  }
  // getAccentColorForUI(), light.
  if (rgb[0] === rgb[1] && rgb[1] === rgb[2]) {
    return `rgb(${rgb.join(', ')})`;
  }
  const [h, s, l] = rgbToHsl(...rgb);
  return `rgb(${hslToRgb(h / 360, Math.min(1, s + 0.3), l * 0.4 + 0.42 * 0.6)
                   .join(', ')})`;
}

function applyPrimaryColor(data) {
  const accent = data.systemAccent ?? cssAccentColor();
  const set = () => document.documentElement.style.setProperty(
      '--zen-primary-color', zenPrimaryColor(accent));
  set();
  matchMedia('(prefers-color-scheme: dark)').addEventListener('change', set);
}

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

// Zen's surface drags the window (-moz-window-dragging: drag) but its card
// (no-drag): a press outside the card and its controls moves the window.
let gWindowDragging = false;
document.addEventListener('mousedown', event => {
  if (event.button !== 0 ||
      event.target.closest('#zen-welcome-pages, button, input, label, a')) {
    return;
  }
  gWindowDragging = true;
  chrome.send('startWindowDrag');
});
// The drag ends with the button: the first move without it (on macOS the
// window server took the release, so the page never saw a mouseup).
document.addEventListener('mousemove', event => {
  if (gWindowDragging && !(event.buttons & 1)) {
    gWindowDragging = false;
    chrome.send('endWindowDrag');
  }
});
document.addEventListener('mouseup', () => {
  gWindowDragging = false;
});

// Zen's welcome is browser chrome, with no page context menu (Back,
// Reload, Inspect...).
document.addEventListener('contextmenu', event => event.preventDefault());

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
    await animate('#zen-welcome', {opacity: [1, 0]});
    // Koan closes the screen and pins the Essentials (KoanWelcome).
    chrome.send('finish', [[...gChoices.essentials]]);
  }
}

// The colours page's panel, where Zen opens its theme picker
// (ZenGradientGenerator.mjs; #41 brings it to Koan): the dot pad with its
// "Click to add a color" and the auto / light / dark buttons, and a row of
// colours. Koan's Spaces have one colour each (a tab group colour) until
// #41's gradients: picking one sets the active Space's, live.
// (As elements, not markup: a WebUI page only takes Trusted Types HTML.)
const kSchemeIcons = {
  auto: [
    ['circle', {cx: 8, cy: 8, r: 6, fill: 'none', stroke: 'currentColor',
                'stroke-width': 1.5}],
    ['path', {d: 'M8 2a6 6 0 0 1 0 12z', fill: 'currentColor'}],
  ],
  light: [
    ['circle', {cx: 8, cy: 8, r: 3, fill: 'currentColor'}],
    ['path', {stroke: 'currentColor', 'stroke-width': 1.5,
              'stroke-linecap': 'round',
              d: 'M8 1v1.5M8 13.5V15M1 8h1.5M13.5 8H15M3 3l1 1M12 12l1 1' +
                  'M3 13l1-1M12 4l1-1'}],
  ],
  dark: [
    ['path', {fill: 'currentColor',
              d: 'M13.5 9.5A6 6 0 0 1 6.5 2.5a6 6 0 1 0 7 7z'}],
  ],
};

function createSchemeIcon(scheme) {
  const kSvg = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(kSvg, 'svg');
  svg.setAttribute('viewBox', '0 0 16 16');
  svg.setAttribute('aria-hidden', 'true');
  for (const [tag, attributes] of kSchemeIcons[scheme]) {
    const shape = document.createElementNS(kSvg, tag);
    for (const [name, value] of Object.entries(attributes)) {
      shape.setAttribute(name, String(value));
    }
    svg.appendChild(shape);
  }
  return svg;
}

// Creates a <div> with `className` and `children`.
function div(className, ...children) {
  const element = document.createElement('div');
  if (className) {
    element.className = className;
  }
  element.append(...children);
  return element;
}

function createThemePicker(colors) {
  const panel = document.createElement('div');
  panel.className = 'zen-welcome-theme-picker';
  const pad = document.createElement('div');
  pad.className = 'zen-theme-picker-gradient';
  const hint = document.createElement('span');
  hint.className = 'zen-theme-picker-click-to-add';
  hint.textContent = 'Click to add a color';
  const schemes = document.createElement('div');
  schemes.className = 'zen-theme-picker-scheme';
  schemes.setAttribute('role', 'radiogroup');
  schemes.setAttribute('aria-label', 'Appearance');
  for (const scheme of ['auto', 'light', 'dark']) {
    const label = document.createElement('label');
    label.title = scheme[0].toUpperCase() + scheme.slice(1);
    const input = document.createElement('input');
    input.type = 'radio';
    input.name = 'zen-theme-picker-scheme';
    input.value = scheme;
    input.checked = scheme === 'auto';
    input.setAttribute('aria-label', label.title);
    input.addEventListener('change', () => chrome.send('setColorScheme', [scheme]));
    label.append(input, createSchemeIcon(scheme));
    schemes.appendChild(label);
  }
  pad.append(hint, schemes);
  const row = document.createElement('div');
  row.className = 'zen-theme-picker-colors';
  row.setAttribute('role', 'radiogroup');
  row.setAttribute('aria-label', 'Colors');
  for (const color of colors) {
    const label = document.createElement('label');
    label.className = 'zen-welcome-color';
    label.title = color.name;
    label.style.setProperty('--zen-welcome-swatch', color.value);
    const input = document.createElement('input');
    input.type = 'radio';
    input.name = 'zen-welcome-color';
    input.value = color.id;
    input.setAttribute('aria-label', color.name);
    const name = document.createElement('span');
    name.className = 'zen-welcome-color-name';
    name.textContent = color.name;
    label.append(input, name);
    input.addEventListener('change', () => chrome.send('setColor', [color.id]));
    row.appendChild(label);
  }
  panel.append(pad, row);
  return panel;
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
        removeVideoBackground();
        if (!data.themePicker) {
          content.appendChild(createThemePicker(data.colors));
          return;
        }
        // ZenWelcome.mjs sizes an anchor from the panel ("20 is the shadow
        // width * 2"; on macOS 90 taller) and opens the panel over it at its
        // top left (position "overlap"). Koan's picker centres on the rect it
        // gets: the panel's own, at the anchor's top left.
        const anchor = document.createElement('div');
        anchor.id = 'zen-welcome-workspace-colors-anchor';
        const {width, height} = data.themePicker;
        anchor.style.width = `${width - 20}px`;
        anchor.style.height = `${data.isMac ? height + 90 : height - 20}px`;
        content.appendChild(anchor);
        const place = () => {
          if (!anchor.isConnected) {
            removeEventListener('resize', place);
            return;
          }
          const r = anchor.getBoundingClientRect();
          chrome.send('showThemePicker', [r.x, r.y, width, height]);
        };
        // Once the page's fade-in has placed it; again when the window
        // resizes (the picker follows).
        requestAnimationFrame(() => requestAnimationFrame(place));
        addEventListener('resize', place);
      },
      leave() {
        chrome.send('closeThemePicker');
      },
    },
    {
      id: 'search',
      title: 'zen-welcome-default-search-title',
      descriptions: ['zen-welcome-default-search-description'],
      buttons: [kNextButton],
      // Skipped when a policy sets the engine (the list is empty).
      skip: () => !data.engines.length,
      // Zen sets the default engine as soon as one is picked; Next also
      // records the choice as made (Chromium's own choice dialog, in the
      // regions that have one, then never asks again).
      render(content) {
        for (const engine of data.engines) {
          const option = createOption({
            id: 'zen-welcome-engine-' + engine.id,
            group: 'zen-welcome-search-engine',
            text: engine.name,
            checked: engine.id === data.defaultEngine,
            extra: createIconWrapper(engine.icon, engine.name),
          });
          const input = option.querySelector('input');
          input.value = engine.id;
          input.addEventListener('change', () => {
            data.defaultEngine = engine.id;
            chrome.send('setSearchEngine', [engine.id, false]);
          });
          content.appendChild(option);
        }
      },
      commit(content) {
        const checked = content.querySelector(
            'input[name=\'zen-welcome-search-engine\']:checked');
        if (checked) {
          chrome.send('setSearchEngine', [Number(checked.value), true]);
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
        // Zen's mock browser: its traffic light dots, the Essentials grid
        // and two tabs.
        const grid = div('');
        grid.id = 'zen-welcome-essentials';
        content.appendChild(div(
            'zen-welcome-mock-browser',
            div('zen-welcome-mock-browser-sidebar',
                div('zen-welcome-mock-browser-dots', div('no-squircles'),
                    div('no-squircles'), div('no-squircles')),
                grid, div('zen-welcome-mock-tab'),
                div('zen-welcome-mock-tab'))));
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
      // Skipped when a policy sets the default browser.
      skip: () => !data.canSetDefaultBrowser,
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
      {delay: 0.1, type: 'spring', stiffness: 300, damping: 20, mass: 1.8});
}

async function start() {
  setText(document.getElementById('zen-welcome-back'), 'zen-welcome-back');
  const data = await sendWithPromise('getWelcomeData');
  if (!data) {
    // Not the first-run screen (the page typed into a tab): nothing to do.
    document.body.replaceChildren();
    return;
  }
  applyPrimaryColor(data);
  animateInitialStage(data);
}

start();
