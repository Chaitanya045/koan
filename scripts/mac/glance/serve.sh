#!/usr/bin/env bash
# Serves the Glance fixtures on two hosts: the parent on
# http://127.0.0.1:8761, the Glance pages on http://localhost:8762 (another
# host, so zoom, which Chromium keeps per host, differs). POST to form.html
# is answered too.
# Stop with Ctrl+C (kills both). Logs go next to this script.
set -euo pipefail
cd "$(dirname "$0")/pages"
log=../serve.log
serve() {
  python3 - "$1" >>"$log" 2>&1 <<'PY' &
import http.server, sys
class H(http.server.SimpleHTTPRequestHandler):
    posts = 0
    def do_POST(self):
        n = int(self.headers.get('Content-Length', 0)); self.rfile.read(n)
        H.posts += 1
        self.do_GET()
    def do_GET(self):
        # /posts: how many POSTs this server has answered (step 10).
        if self.path == '/posts':
            body = str(H.posts).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()
http.server.ThreadingHTTPServer(('127.0.0.1', int(sys.argv[1])), H).serve_forever()
PY
}
serve 8761
serve 8762
trap 'kill $(jobs -p) 2>/dev/null' EXIT INT TERM
echo "parent: http://127.0.0.1:8761/parent.html"
wait
