#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tiny HTTP server for scripts/benchmark.sh.

Serves the benchmark pages and records when a page's beacon arrives:
GET /mark?run=<id> appends "<id> <monotonic seconds>" to the marks file.
The browser-independent clock is the server's, so startup times compare
across engines.
"""

import http.server
import sys
import time
import urllib.parse

PAGE = """<!doctype html><title>bench</title>
<body><h1>Koan benchmark page</h1>
<script>
addEventListener('load', () => fetch('/mark?run=' + location.hash.slice(1)));
</script>
<div id="filler"></div>
<script>
const f = document.getElementById('filler');
for (let i = 0; i < 2000; i++) {
  const p = document.createElement('p');
  p.textContent = 'Paragraph ' + i + ' with some text to lay out and paint.';
  f.append(p);
}
</script>
</body>"""


class Handler(http.server.BaseHTTPRequestHandler):
  marks_path = None

  def do_GET(self):
    url = urllib.parse.urlparse(self.path)
    if url.path == '/mark':
      run = urllib.parse.parse_qs(url.query).get('run', ['?'])[0]
      with open(self.marks_path, 'a') as f:
        f.write(f'{run} {time.monotonic():.6f}\n')
      body = b'ok'
    else:
      body = PAGE.encode()
    self.send_response(200)
    self.send_header('Content-Type', 'text/html')
    self.send_header('Cache-Control', 'no-store')
    self.send_header('Content-Length', str(len(body)))
    self.end_headers()
    self.wfile.write(body)

  def log_message(self, *args):
    pass


def main():
  port, marks = int(sys.argv[1]), sys.argv[2]
  Handler.marks_path = marks
  http.server.ThreadingHTTPServer(('127.0.0.1', port), Handler).serve_forever()


if __name__ == '__main__':
  main()
