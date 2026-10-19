#!/usr/bin/env bash
# Benchmarks the Koan release build against the installed Chromium and
# Firefox (Zen's engine) and records the results.
#
#   scripts/benchmark.sh [--runs N] [--no-speedometer]
#
# Run on an otherwise idle machine, after KOAN_CONFIG=release
# scripts/build.sh. Writes docs/benchmarks/<date>.json and prints a Markdown
# table for docs/BENCHMARKS.md.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/docs/benchmarks"
exec python3 -I "$ROOT/scripts/bench/bench.py" \
  --json "$ROOT/docs/benchmarks/$(date +%F).json" "$@"
