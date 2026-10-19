// Usage: node scripts/cdp_windows.mjs <port>
// Opens two extra windows via CDP, closes one, then closes the browser.
// Used to exercise BrowserView create/destroy hooks.
const port = process.argv[2];
const v = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
const ws = new WebSocket(v.webSocketDebuggerUrl);
let id = 0; const pending = new Map();
const send = (method, params = {}) => new Promise(r => { pending.set(++id, r); ws.send(JSON.stringify({ id, method, params })); });
ws.onmessage = e => { const m = JSON.parse(e.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } };
await new Promise(r => (ws.onopen = r));
const sleep = ms => new Promise(r => setTimeout(r, ms));
const a = await send('Target.createTarget', { url: 'about:blank', newWindow: true });
const b = await send('Target.createTarget', { url: 'about:blank', newWindow: true });
await sleep(1500);
console.log('closing one window:', (await send('Target.closeTarget', { targetId: a.result.targetId })).result);
await sleep(1500);
await send('Browser.close');
ws.close();
