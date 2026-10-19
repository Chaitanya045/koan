// Usage: node scripts/cdp_key.mjs <port> <windowsKeyCode> <modifiers> [key]
// Sends a key press to the active page of a Koan started with
// --remote-debugging-port=<port>. Keys the page doesn't handle bubble back
// to the browser (BrowserView::HandleKeyboardEvent -> FocusManager), so this
// exercises browser accelerators the real way. Modifiers: Alt=1, Ctrl=2,
// Meta=4, Shift=8.
const [port, code, modifiers = '0', key = ''] = process.argv.slice(2);
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
const base = { windowsVirtualKeyCode: Number(code), nativeVirtualKeyCode: Number(code),
               modifiers: Number(modifiers), key };
await send('Input.dispatchKeyEvent', { ...base, type: 'rawKeyDown' });
const r = await send('Input.dispatchKeyEvent', { ...base, type: 'keyUp' });
console.log(JSON.stringify(r.error ?? 'sent'));
ws.close();
