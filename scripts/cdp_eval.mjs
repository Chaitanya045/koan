// Usage: node scripts/cdp_eval.mjs <port> <url> <js-expression> [screenshot.png]
// Navigates the first page of a running Koan (started with
// --remote-debugging-port=<port>) to <url>, waits for load, evaluates the
// expression and prints the result. Works for chrome:// URLs in headless.
import { writeFileSync } from 'node:fs';
const [port, url, expr, shot] = process.argv.slice(2);
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
await send('Page.enable');
await send('Page.navigate', { url });
await new Promise(r => setTimeout(r, 2500));
const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
console.log(r.result.exceptionDetails ? JSON.stringify(r.result.exceptionDetails) : r.result.result.value);
if (shot) {
  const s = await send('Page.captureScreenshot', { format: 'png' });
  writeFileSync(shot, Buffer.from(s.result.data, 'base64'));
}
ws.close();
