// Usage: node scripts/cdp_click.mjs <port> <x> <y> [modifiers]
// Clicks at page coordinates (CSS px) in the first page target of a Koan
// started with --remote-debugging-port=<port>. Modifiers: Alt=1, Ctrl=2,
// Meta=4, Shift=8. Input arrives as trusted user input.
const [port, x, y, modifiers = '0'] = process.argv.slice(2);
const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
const ws = new WebSocket(targets.find(t => t.type === 'page').webSocketDebuggerUrl);
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
const base = { x: Number(x), y: Number(y), modifiers: Number(modifiers), button: 'left', clickCount: 1 };
await send('Input.dispatchMouseEvent', { ...base, type: 'mouseMoved', button: 'none' });
await send('Input.dispatchMouseEvent', { ...base, type: 'mousePressed' });
const r = await send('Input.dispatchMouseEvent', { ...base, type: 'mouseReleased' });
console.log(JSON.stringify(r.error ?? 'clicked'));
ws.close();
