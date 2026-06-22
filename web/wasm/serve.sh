#!/usr/bin/env bash
# Serve the spike so a browser can fetch the .wasm (file:// won't work).
#   scripts: ./serve.sh [port]   then open the printed URL
set -euo pipefail
cd "$(dirname "$0")"
PORT="${1:-8782}"
echo "Serving — open:  http://localhost:${PORT}/index.html"
exec python3 -m http.server "${PORT}"
