// Usage: node scripts/ui_tree.mjs <ui-devtools-port> [filter-regex]
// Dumps the native views tree of a running Koan started with
// --enable-ui-devtools=<port>: one line per node with class name and bounds
// (x,y relative to the parent view, w x h, "hidden" if not visible). Use with shot_ui.sh-style
// headless Ozone runs to debug layout without touching the desktop.
const [port, filter] = process.argv.slice(2);
const re = filter ? new RegExp(filter) : null;
// The UI DevTools server has no HTTP endpoints; connect straight to client 0.
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
await send('CSS.enable');
const doc = (await send('DOM.getDocument', { depth: -1 })).result.root;
// UI DevTools reports view metadata (X/Y/Width/Height relative to the parent,
// Visible, Background, ...) as "CSS" properties of matched rules.
async function bounds(nodeId) {
  const r = await send('CSS.getMatchedStylesForNode', { nodeId });
  const props = {};
  for (const rule of r.result?.matchedCSSRules ?? []) {
    for (const p of rule.rule.style.cssProperties) props[p.name] = p.value;
  }
  if (!('X' in props)) return '';
  const hidden = props.Visible === 'false' ? ' hidden' : '';
  return `${props.X},${props.Y} ${props.Width}x${props.Height}${hidden}`;
}

async function walk(n, depth) {
  const name = n.attributes ? n.attributes[n.attributes.indexOf('name') + 1] ?? n.nodeName : n.nodeName;
  const line = `${'  '.repeat(depth)}${n.nodeName === name ? name : `${n.nodeName} ${name}`}`;
  // Never query the root node: RootElement::GetBounds() is NOTREACHED and
  // crashes the browser.
  if (depth > 0 && (!re || re.test(line))) {
    console.log(`${line}  [${await bounds(n.nodeId)}]`);
  }
  for (const c of n.children ?? []) await walk(c, depth + 1);
}
await walk(doc, 0);
ws.close();
