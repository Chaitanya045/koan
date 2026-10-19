// Usage:
//   node scripts/ui_input.mjs <port> <class-regex> <nth> mouse <type> [x y]
//   node scripts/ui_input.mjs <port> <class-regex> <nth> key <keyCode> <flags>
//
// Injects a native UI event into the <nth> (0-based) view whose class name
// matches <class-regex>, in a Koan started with --enable-ui-devtools=<port>
// (see ui_session.sh). Mouse types: mousePressed, mouseReleased, mouseMoved,
// mouseEntered, mouseExited. x/y are relative to the view. Key flags are
// ui::EventFlags (Ctrl = 4, Shift = 2, Alt = 8). Prints the matched views.
const [port, classRe, nthArg, kind, ...rest] = process.argv.slice(2);
const ws = new WebSocket(`ws://127.0.0.1:${port}/0`);
let id = 0;
const pending = new Map();
const send = (method, params = {}) => new Promise(resolve => {
  pending.set(++id, resolve);
  ws.send(JSON.stringify({ id, method, params }));
});
ws.onmessage = e => {
  const m = JSON.parse(e.data);
  if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
};
await new Promise(r => (ws.onopen = r));
await send('DOM.enable');
const doc = (await send('DOM.getDocument', { depth: -1 })).result.root;
const re = new RegExp(classRe);
const matches = [];
(function walk(n, depth) {
  const attrs = n.attributes ?? [];
  const cls = attrs[attrs.indexOf('class') + 1];
  if (depth > 0 && n.nodeName === 'View' && cls && re.test(cls)) matches.push(n);
  for (const c of n.children ?? []) walk(c, depth + 1);
})(doc, 0);
console.log(`${matches.length} match(es) for /${classRe}/`);
const target = matches[Number(nthArg)];
if (!target) { console.log('no such view'); process.exit(1); }
let r;
if (kind === 'mouse') {
  const [type, x = '1', y = '1'] = rest;
  r = await send('DOM.dispatchMouseEvent', {
    nodeId: target.nodeId,
    event: { type, x: Number(x), y: Number(y), button: type.includes('Pressed') || type.includes('Released') ? 'left' : 'none', wheelDirection: 'none' },
  });
} else if (kind === 'key') {
  const [keyCode, flags = '0'] = rest;
  for (const type of ['keyPressed', 'keyReleased']) {
    r = await send('DOM.dispatchKeyEvent', {
      nodeId: target.nodeId,
      event: { type, keyCode: Number(keyCode), code: 0, flags: Number(flags), key: 0, isChar: false },
    });
  }
}
console.log(JSON.stringify(r.error ?? r.result));
ws.close();
