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
#   sh tools/cycles.sh            compare against tools/cycles_baseline.txt
#   sh tools/cycles.sh --update   rewrite the baseline (say why in the commit)
#
# Cycles are ESTIMATED from a Cortex-M3 timing table, not counted on hardware:
# multiplies 4, loads/stores 2, a taken branch 3, IT folded to 0, everything
# else 1. Good for DELTAS, which is what this is for; do not quote it as an
# absolute.
set -e
cd "$(dirname "$0")/.."
ELF=build/yarns/yarns.elf
[ -f "$ELF" ] || { echo "no $ELF -- run 'make firmware' first"; exit 1; }

DIS=$(mktemp)
SKIP_PROGRAMMING=true ./env/mutable-env.sh \
  /usr/local/arm-4.8.3/bin/arm-none-eabi-objdump -d "$ELF" > "$DIS" 2>/dev/null
python3 tools/cycles.py "$DIS" "$@"
STATUS=$?
rm -f "$DIS"
exit $STATUS
