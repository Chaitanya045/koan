// Serves scripts/e2e/pages over http on a free local port (dedicated
// workers and Glance need an http origin).
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { dirname, join, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..', 'e2e', 'pages');

export async function servePages() {
  const server = createServer(async (req, res) => {
    const path = normalize(decodeURIComponent(new URL(req.url, 'http://x').pathname));
    try {
      const body = await readFile(join(root, path));
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end(body);
    } catch {
      res.writeHead(404);
      res.end();
    }
  });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  // The browser keeps connections alive; drop them so the process can exit.
  const close = () => {
    server.closeAllConnections();
    server.close();
  };
  return { origin: `http://127.0.0.1:${server.address().port}`, close };
}
