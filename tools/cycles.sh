#!/bin/sh
# PER-SAMPLE CYCLE COUNT for the envelope's render loop, against a recorded
# baseline. Run it on every change that touches yarns/envelope.cc.
#
# This exists because the count kept being ASSERTED instead of measured. The
# loop runs 12 times per sample (4 CV outputs, plus 4 paraphonic voices x
# timbre and gain), so one instruction here is ~0.7% of the whole CPU at 45 kHz
# on a 72 MHz M3 -- and a change that "obviously" removes work can add it once
# the compiler is done. Measure, then claim.
#
#   sh tools/cycles.sh                    price the current build
#   sh tools/cycles.sh --against OLD.elf  and diff it against another build
#
# THERE IS NO STORED BASELINE, deliberately. This used to compare against a
# committed tools/cycles_baseline.txt, which meant every perf commit dragged a
# regenerated snapshot along, and -- worse -- the snapshot was priced by
# whatever model was current when it was written. When the cost model moved
# (2026-09-06, branches), every line of it read as a regression and the real
# deltas were buried. Two builds priced by TODAY\'s model move together, so a
# comparison made this way cannot go stale.
#
# Keep the build you want to compare with: `cp build/yarns/yarns.elf /tmp/x.elf`
# before the change, or build a git ref in a worktree.
#
# Cycles are ESTIMATED from a Cortex-M3 timing table, not counted on hardware:
# multiplies 4, loads/stores 2, a taken branch 3, IT folded to 0, everything
# else 1. Good for DELTAS, which is what this is for; do not quote it as an
# absolute.
set -e
cd "$(dirname "$0")/.."
ELF=build/yarns/yarns.elf
[ -f "$ELF" ] || { echo "no $ELF -- run 'make firmware' first"; exit 1; }

# objdump runs in the container, which mounts the REPO and nothing else, so an
# elf from anywhere else is invisible to it. Stage it in build/ first, which is
# gitignored, rather than fail with an empty disassembly.
disassemble() {
  case "$1" in
    build/*|./build/*) target="$1" ;;
    *) target="build/yarns/cycles_compare.elf"; cp "$1" "$target" ;;
  esac
  SKIP_PROGRAMMING=true ./env/mutable-env.sh \
    /usr/local/arm-4.8.3/bin/arm-none-eabi-objdump -d "$target" 2>/dev/null
  [ "$target" = "build/yarns/cycles_compare.elf" ] && rm -f "$target"
  return 0
}

DIS=$(mktemp)
disassemble "$ELF" > "$DIS"

if [ "$1" = "--against" ]; then
  [ -f "$2" ] || { echo "no such build: $2"; rm -f "$DIS"; exit 1; }
  OLD_DIS=$(mktemp); OLD_METRICS=$(mktemp); NEW_METRICS=$(mktemp)
  disassemble "$2" > "$OLD_DIS"
  python3 tools/cycles.py "$OLD_DIS" --metrics > "$OLD_METRICS"
  python3 tools/cycles.py "$DIS" --metrics > "$NEW_METRICS"
  python3 tools/cycles.py "$DIS"
  echo "  --- against $2, both priced by the model in this tree ---"
  # Same keys in the same order from both runs, so a plain paste lines them up.
  paste "$OLD_METRICS" "$NEW_METRICS" | awk '
    $1 == $3 && $2 != $4 {
      printf "  %-10s %-22s %s -> %s\n", ($4 > $2 ? "REGRESSION" : "improved"), $1, $2, $4
      if ($4 > $2) worse = 1
    }
    END { if (!worse) print "  nothing got worse" }'
  rm -f "$DIS" "$OLD_DIS" "$OLD_METRICS" "$NEW_METRICS"
  exit 0
fi

python3 tools/cycles.py "$DIS" "$@"
STATUS=$?
rm -f "$DIS"
exit $STATUS
