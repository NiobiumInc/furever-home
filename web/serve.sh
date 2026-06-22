#!/usr/bin/env bash
# Serve the web app for local use. Serves the repo root so the pages can reach
# both web/ (the app + wasm) and dsl/rubric.dat (the shelter's plaintext rubric).
#   web/serve.sh [port]   then open the printed URL
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"   # pet-adoption repo root
PORT="${1:-8800}"
cd "$ROOT"
echo "Serving ${ROOT}"
echo "open:  http://localhost:${PORT}/web/index.html"
exec /opt/homebrew/bin/python3 -m http.server "${PORT}"
