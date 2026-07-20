#!/bin/sh
# One command to establish whether the envelope work is sound.
#
# Exists so an agent can close its own loop: every check below runs the REAL
# yarns/envelope.cc and reports pass/fail without anyone listening to hardware.
# Run it after any change to the envelope, the engine, or the sim.
#
#   tools/check.sh            everything except the firmware build
#   tools/check.sh --fw       also cross-build the firmware (slow, needs Docker)
#
# Rebuilding the Emscripten engine needs Docker too, so it is skipped unless
# the engine is missing or --engine is passed. If yarns/envelope.cc changed,
# pass --engine or the sim keeps running the PREVIOUS firmware.
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
fails=0
step() { printf '\n=== %s ===\n' "$1"; }
try() { if "$@"; then :; else fails=$((fails + 1)); echo "  ^^ FAILED"; fi; }

case " $* " in *" --fw "*)
  step "firmware cross-build"
  try env SKIP_PROGRAMMING=true ./env/mutable-env.sh make -f yarns/makefile syx
;; esac

step "host battery (22 checks on the native build)"
try sh tools/hosttest/build.sh

step "bit-exact golden vectors"
try node tools/hosttest/golden.js

step "setting-space anomaly sweep vs baseline"
try node tools/hosttest/anomaly.js

case " $* " in *" --engine "*) rebuild=yes ;; *)
  [ -f tools/simengine/chiff_engine.js ] || rebuild=yes ;;
esac
if [ "$rebuild" = yes ]; then
  step "rebuild sim engine from yarns/envelope.cc"
  try sh tools/simengine/build.sh
else
  echo "\n(sim engine not rebuilt; pass --engine after editing yarns/envelope.cc)"
fi

step "engine == native build"
try node tools/simengine/parity.js

step "sim page == native build"
try node tools/chiff_checks/simparity.js

printf '\n'
if [ "$fails" -eq 0 ]; then
  echo "ALL CHECKS PASSED"
else
  echo "$fails CHECK GROUP(S) FAILED"
fi
exit "$fails"
