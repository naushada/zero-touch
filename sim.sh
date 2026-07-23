#!/usr/bin/env bash
#
# sim.sh — build (if needed) and run the offline SMS simulator.
#
# zerotouch-sim bypasses the SMS transport entirely: it wires the REAL Bridge
# (tokenise → parse → session/auth → gnmi/classic executor → reply) behind an
# in-process ConsoleTransport, so each line you type lands straight at
# Bridge::on_sms — no modem, no ds-server, no gRPC. The gNMI backend is an
# in-memory tree (MemGnmiSink). See DESIGN.md / README.md.
#
# Usage:
#   ./sim.sh [--rebuild]      # --rebuild forces a fresh build first
#
# Then type SMS bodies at the `>` prompt, e.g.:
#   IOT LOGIN admin admin
#   IOT GNMI GET /system/config/hostname
# REPL commands: /from <num> /enable /disable /allow <csv> /tree /users /help /quit

set -euo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"

REBUILD=0
[ "${1:-}" = "--rebuild" ] && { REBUILD=1; shift; }

BIN="build/zerotouch-sim"
if [ "$REBUILD" = 1 ] || [ ! -x "$BIN" ]; then
    ./build.sh --sim
fi

exec "$BIN" "$@"
