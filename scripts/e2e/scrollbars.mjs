// End-to-end test of Koan's scrollbars against a real build: thin overlay
// scrollbars, like Zen's, that take no layout space in pages or in
// scrollable elements.
//
//   scripts/e2e/run.sh scrollbars
const CDP_PORT = process.env.CDP_PORT;

// Evaluates `expression` in the test page; resolves undefined after 5 s so
// a wedged renderer fails the check instead of hanging the run.
async function evaluate(expression) {
  const page = (await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/list`)).json())
      .find(t => t.type === 'page' && t.url.endsWith('/scrollbars.html'));
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise(r => (ws.onopen = r));
  const result = await new Promise(resolve => {
    ws.onmessage = e => {
      const m = JSON.parse(e.data);
      if (m.id === 1) resolve(m.result?.result?.value);
    };
    ws.onclose = () => resolve(undefined);
    setTimeout(() => resolve(undefined), 5000);
    ws.send(JSON.stringify({ id: 1, method: 'Runtime.evaluate',
                             params: { expression, returnByValue: true } }));
  });
  ws.close();
  return result;
}

let failures = 0;
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures++;
}

const page = await evaluate('innerWidth - document.documentElement.clientWidth');
check('The page scrollbar takes no space (overlay)', page === 0, `${page}px`);
const box = await evaluate(
    '(b => b.offsetWidth - b.clientWidth)(document.getElementById("box"))');
check('Scrollable elements too', box === 0, `${box}px`);
const scrolls = await evaluate(
    'document.documentElement.scrollHeight > innerHeight');
check('...and the page still scrolls', scrolls === true);

console.log(failures ? `${failures} check(s) failed` : 'all checks passed');
process.exit(failures ? 1 : 0);
