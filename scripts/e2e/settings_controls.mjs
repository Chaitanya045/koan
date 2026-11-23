// End-to-end test of Chromium's switches and radio buttons in chrome://settings
// in Koan's looks (#419): on chrome://settings/performance, in light and dark
// mode, every visible cr-toggle (through the shadow DOM) shows "on" as the
// primary track with the thumb at the right edge and "off" as the muted track
// with the thumb at the left, the knob's transform travels between them, and
// an unselected radio ring (Memory Saver's) stands out from the card by at
// least 3:1 (WCAG 1.4.11). Then the Zen look, which keeps Chromium's own
// controls, gets the same position checks.
//
//   scripts/e2e/run.sh settings_controls
//
// With SHOTS=<dir>, saves settings-controls-<look>-<mode>-before.png (the
// switches as the profile has them) and ...-after.png (each one flipped).
// The prefs it flips are put back.
import {
  browserCall, check, evalIn, finish, onPage, pages, settingsCall, sleep, waitFor,
} from './lib/koan.mjs';
import fs from 'node:fs';

const SHOTS = process.env.SHOTS;

async function shot(page, name, colorScheme) {
  if (!SHOTS) return;
  // Emulation lasts as long as the session, so capture in the same one.
  const r = await onPage(page, async c => {
    await c.send('Emulation.setEmulatedMedia',
        { features: [{ name: 'prefers-color-scheme', value: colorScheme }] });
    await sleep(300);
    return c.send('Page.captureScreenshot', { format: 'png' });
  });
  fs.writeFileSync(`${SHOTS}/${name}.png`, Buffer.from(r.result.data, 'base64'));
}

const targetId = (await browserCall('Target.createTarget',
    { url: 'chrome://settings/performance' })).result.targetId;
const settings = await waitFor(async () => (await pages()).find(p => p.id === targetId), 8000);
check('chrome://settings/performance opens', !!settings);
await waitFor(() => evalIn(settings,
    `!!document.querySelector('settings-ui')?.shadowRoot?.querySelector('#main')`), 8000);

// Through every shadow root of the page (Koan's panes' excluded).
const DEEP = `const findAll = (root, s) => [...root.querySelectorAll(s),
    ...[...root.querySelectorAll('*')].filter(el => el.shadowRoot &&
        el.shadowRoot !== window.koanSettingsRoot).flatMap(el => findAll(el.shadowRoot, s))];`;
// Colours as sRGB bytes, composited over what's behind them (a 1 px canvas
// parses any CSS colour: oklch, color-mix, color(srgb ...)).
const COLOR = `const paint = (...colors) => {
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = 1;
    const ctx = canvas.getContext('2d', { willReadFrequently: true });
    for (const c of colors) { ctx.fillStyle = c; ctx.fillRect(0, 0, 1, 1); }
    return [...ctx.getImageData(0, 0, 1, 1).data].slice(0, 3);
  };
  const luminance = rgb => {
    const [r, g, b] = rgb.map(v => v / 255).map(
        v => v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4);
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
  };
  const contrast = (a, b) => {
    const [hi, lo] = [luminance(a), luminance(b)].sort((x, y) => y - x);
    return (hi + 0.05) / (lo + 0.05);
  };`;

// The visible switches: their geometry, transform and colours.
const toggles = () => evalIn(settings, `(() => { ${DEEP}
  return findAll(document, 'cr-toggle').filter(t => t.checkVisibility()).map((t, i) => {
    const bar = t.shadowRoot.querySelector('#bar');
    const knob = t.shadowRoot.querySelector('#knob');
    const b = bar.getBoundingClientRect(), k = knob.getBoundingClientRect();
    const transform = getComputedStyle(knob).transform;
    const m = transform.match(/matrix\\(([^)]+)\\)/);
    return {
      i, checked: t.checked, disabled: t.disabled,
      label: (t.getAttribute('aria-label') ||
              t.getRootNode().host?.getAttribute('label') || '').slice(0, 40),
      startGap: k.left - b.left, endGap: b.right - k.right,
      knobCentre: k.left + k.width / 2 - b.left, barWidth: b.width, knobWidth: k.width,
      transform, translateX: m ? Number(m[1].split(',')[4]) : 0,
      track: getComputedStyle(bar).backgroundColor,
    };
  });
})()`);
const flip = indices => evalIn(settings, `(() => { ${DEEP}
  const all = findAll(document, 'cr-toggle').filter(t => t.checkVisibility());
  for (const i of ${JSON.stringify(indices)}) all[i]?.click();
})()`);

async function checkSwitches(where, tight) {
  const before = await toggles();
  check(`${where}: the page shows switches`, before?.length > 0, JSON.stringify(before?.length));
  // Flip each enabled one, so both states are seen on every switch.
  const flippable = (before ?? []).filter(t => !t.disabled).map(t => t.i);
  await flip(flippable);
  await sleep(600);  // the 150 ms move, and Memory Saver's collapse
  const after = await toggles();
  const states = [...(before ?? []), ...(after ?? [])];
  // Shadcn's Switch: the thumb on the track's 1 px border at either end.
  // Chromium's own (Zen look): a few px in, but past the middle.
  const edge = tight ? 1.5 : 5;
  for (const t of states) {
    const name = `${where}: "${t.label || 'switch ' + t.i}" ${t.checked ? 'on' : 'off'}`;
    const middle = t.barWidth / 2;
    if (t.checked) {
      check(`${name}: the thumb is at the right edge`,
            t.endGap <= edge && t.knobCentre > middle, JSON.stringify(t));
    } else {
      check(`${name}: the thumb is at the left edge`,
            t.startGap <= edge && t.knobCentre < middle, JSON.stringify(t));
    }
  }
  for (const i of flippable) {
    const a = before.find(t => t.i === i), b = after?.find(t => t.i === i);
    if (!b || a.checked === b.checked) {
      check(`${where}: switch ${i} flips when clicked`, false, JSON.stringify([a, b]));
      continue;
    }
    const [on, off] = a.checked ? [a, b] : [b, a];
    check(`${where}: "${a.label || i}": the knob's transform travels (on is not none/0)`,
          on.transform !== 'none' && on.translateX - off.translateX >= (tight ? 12 : 6),
          JSON.stringify([off.transform, on.transform]));
    check(`${where}: "${a.label || i}": on and off tracks differ`, on.track !== off.track,
          JSON.stringify([off.track, on.track]));
  }
  return { flippable };
}

// Memory Saver's radios: its toggle on (the group shows), then the rings.
async function checkRadios(where, mode) {
  const opened = () => evalIn(settings, `(() => { ${DEEP}
    return findAll(document, '#radioGroupCollapse')[0]?.opened === true;
  })()`);
  let turnedOn = false;
  if (!await opened()) {
    await evalIn(settings, `(() => { ${DEEP}
      findAll(document, 'settings-memory-page')[0]?.shadowRoot
          .querySelector('#toggleButton')?.shadowRoot.querySelector('cr-toggle')?.click();
    })()`);
    turnedOn = true;
  }
  check(`${where}: Memory Saver's choices show`, await waitFor(opened, 4000));
  await sleep(500);
  const r = await evalIn(settings, `(() => { ${DEEP} ${COLOR}
    const page = findAll(document, 'settings-memory-page')[0]?.shadowRoot;
    const card = page?.querySelector('settings-section')?.shadowRoot.querySelector('#card');
    const under = [getComputedStyle(document.documentElement).backgroundColor,
                   getComputedStyle(document.body).backgroundColor,
                   card && getComputedStyle(card).backgroundColor].filter(Boolean);
    const probe = document.createElement('div');
    probe.style.color = 'var(--primary)';
    document.body.append(probe);
    const primary = paint(...under, getComputedStyle(probe).color);
    probe.remove();
    const cardRgb = paint(...under);
    return findAll(page ?? document, 'controlled-radio-button').map(radio => {
      const ring = getComputedStyle(radio.shadowRoot.querySelector('.disc-border'));
      const rgb = paint(...under, ring.borderTopColor);
      return { id: radio.id, checked: radio.checked, ring: ring.borderTopColor,
               width: ring.borderTopWidth, contrast: +contrast(rgb, cardRgb).toFixed(2),
               primary: rgb.every((v, i) => Math.abs(v - primary[i]) <= 2) };
    });
  })()`);
  const unchecked = (r ?? []).filter(x => !x.checked);
  check(`${where}: Memory Saver has unselected choices`, unchecked.length > 0, JSON.stringify(r));
  for (const x of unchecked) {
    check(`${where}: unselected "${x.id}" ring has >= 3:1 contrast with the card (${mode})`,
          x.contrast >= 3, JSON.stringify(x));
  }
  const selected = (r ?? []).find(x => x.checked);
  check(`${where}: the selected choice's ring is the primary colour`, selected?.primary === true,
        JSON.stringify(selected));
  return turnedOn;
}

async function setLook(style, mode) {
  await settingsCall(settings, 'koanSetPref', 'koan.look.style', style);
  await settingsCall(settings, 'koanSetPref', 'koan.look.mode', mode);
  // The look's stylesheet sets color-scheme on :root (empty in the Zen style).
  await waitFor(() => evalIn(settings,
      `getComputedStyle(document.documentElement).colorScheme.includes('${mode}')`), 5000);
  await sleep(800);
}

const prefs = (await settingsCall(settings, 'koanGetSettings'))?.prefs ?? {};
for (const [style, tight] of [['shadcn', true], ['zen', false]]) {
  for (const mode of ['light', 'dark']) {
    await setLook(style, mode);
    const where = `${style} ${mode}`;
    await shot(settings, `settings-controls-${style}-${mode}-before`, mode);
    const { flippable } = await checkSwitches(where, tight);
    await shot(settings, `settings-controls-${style}-${mode}-after`, mode);
    // Put the prefs back: flip the same switches again.
    await flip(flippable);
    await sleep(600);
    if (style === 'shadcn') {
      const turnedOn = await checkRadios(where, mode);
      await shot(settings, `settings-controls-${style}-${mode}-radios`, mode);
      if (turnedOn) {
        await evalIn(settings, `(() => { ${DEEP}
          findAll(document, 'settings-memory-page')[0]?.shadowRoot
              .querySelector('#toggleButton')?.shadowRoot.querySelector('cr-toggle')?.click();
        })()`);
        await sleep(400);
      }
    }
  }
}
await settingsCall(settings, 'koanSetPref', 'koan.look.style', prefs['koan.look.style'] ?? 'shadcn');
await settingsCall(settings, 'koanSetPref', 'koan.look.mode', prefs['koan.look.mode'] ?? 'dark');

finish();
