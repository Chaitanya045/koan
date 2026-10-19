// Usage: node scripts/cdp_close.mjs <port>
// Closes a Koan started with --remote-debugging-port=<port> gracefully
// (Browser.close), so session state is written like a normal quit.
const port = process.argv[2];
const v = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
const ws = new WebSocket(v.webSocketDebuggerUrl);
await new Promise(r => (ws.onopen = r));
ws.send(JSON.stringify({ id: 1, method: 'Browser.close' }));
await new Promise(r => setTimeout(r, 500));
ws.close();
