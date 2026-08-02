#!/bin/sh
# Differential verification of the render-loop ARM asm.
#
# The hand-written asm (#if __arm__ path in yarns/envelope.cc), run under QEMU
# on an emulated Cortex-M3, must produce output BIT-IDENTICAL to the C reference
# (the #else path, compiled on the host by clang). Both are driven by the SAME
# tools/hosttest/driver.cc with the SAME deterministic PRNG, so any difference
# is a real asm-vs-C divergence -- the one bug class ears and the sim can't see.
#
# Runs the whole scenario battery. Exit 0 iff every scenario matches.
set -e
cd "$(dirname "$0")"
ROOT=../..
AMOUNT=96
DURATION=90
SCENARIOS="basic early_release retrigger inverted latehang held"
# Scenarios re-run with a LIVE BIAS. Everything above renders bias == 0, which
# meant the asm-vs-C differential never once executed the bias arithmetic --
# and the render loop carries envelope PLUS bias, so the ramp and the recovery
# were unverified against the C reference. Two scenarios are enough to cover
# the path without doubling an already slow double-emulated run.
BIAS_SCENARIOS="basic held"
TREMOLO=24000
# An independent, timbre-LFO-style bias, which is the only way to make the
# clamp BITE: tremolo is scaled to the envelope so the sum stays near range.
# The asm saturates with USAT and the C reference with two compares, and this
# is the only scenario where those two have to agree under saturation.
BIAS_LFO=20000
# A note whose range sits BELOW zero (a negative TIMBRE MOD ENV target). Every
# scenario above has a non-negative floor, so the clamp offset that lets the
# value go negative was unverified against the C reference -- and this is the
# case where the asm and the C reference are least obviously equal: the asm
# saturates with USAT #15, ASR #15 (one instruction, arithmetic shift then
# saturate) where the C does >> 15 then two compares, and only a negative
# operand exercises the sign path through both.
NEG_SCENARIOS="basic held"
NEG_RANGE=-16383

echo "== building host C reference (clang) =="
( cd ../hosttest && python3 ../portable_envelope.py ../.. envelope_host.cc &&
  clang++ -std=c++11 -O1 -w -DTEST -I shim -I ../.. \
    envelope_host.cc ../../yarns/resources.cc driver.cc -o test )

echo "== building ARM asm ELF + running every scenario under QEMU =="
# One docker call: build the ELF once, then run each scenario. $s expands in the
# container's shell (the loop runs there); sample files land in tools/qemutest.
# Run the wrapper from the repo root so it mounts the repo (not tools/qemutest)
# as /workdir; the in-container paths below are repo-root-relative.
( cd "$ROOT" && SKIP_PROGRAMMING=true ./env/mutable-env.sh 'set -e
  sh tools/qemutest/build.sh
  cd tools/qemutest
  for s in '"$SCENARIOS"'; do
    qemu-system-arm -M lm3s6965evb -nographic -semihosting \
      -semihosting-config arg=test,arg=$s,arg='"$AMOUNT"',arg='"$DURATION"',arg=hash=1 \
      -kernel test.elf
    mv qemu_out.txt "qemu_$s.txt"
  done
  for s in '"$BIAS_SCENARIOS"'; do
    qemu-system-arm -M lm3s6965evb -nographic -semihosting \
      -semihosting-config arg=test,arg=$s,arg='"$AMOUNT"',arg='"$DURATION"',arg=hash=1,arg=tremolo='"$TREMOLO"',arg=bias_lfo='"$BIAS_LFO"' \
      -kernel test.elf
    mv qemu_out.txt "qemu_${s}_bias.txt"
  done
  for s in '"$NEG_SCENARIOS"'; do
    qemu-system-arm -M lm3s6965evb -nographic -semihosting \
      -semihosting-config arg=test,arg=$s,arg='"$AMOUNT"',arg='"$DURATION"',arg=hash=1,arg=range='"$NEG_RANGE"',arg=bias_lfo='"$BIAS_LFO"' \
      -kernel test.elf
    mv qemu_out.txt "qemu_${s}_neg.txt"
  done' ) >/dev/null

echo "== diffing asm vs C, per scenario =="
fail=0
for s in $SCENARIOS; do
  ../hosttest/test "$s" "$AMOUNT" "$DURATION" hash=1 > "host_$s.txt"
  if cmp -s "host_$s.txt" "qemu_$s.txt"; then
    printf 'PASS %-14s hash %s  (asm == C)\n' "$s" "$(cat qemu_$s.txt)"
  else
    printf 'FAIL %-14s host=%s qemu=%s\n' "$s" "$(cat host_$s.txt)" "$(cat qemu_$s.txt)"
    fail=1
  fi
done
for s in $BIAS_SCENARIOS; do
  ../hosttest/test "$s" "$AMOUNT" "$DURATION" hash=1 "tremolo=$TREMOLO" \
    "bias_lfo=$BIAS_LFO" > "host_${s}_bias.txt"
  if cmp -s "host_${s}_bias.txt" "qemu_${s}_bias.txt"; then
    printf 'PASS %-9s bias hash %s  (asm == C)\n' "$s" "$(cat qemu_${s}_bias.txt)"
  else
    printf 'FAIL %-9s bias host=%s qemu=%s\n' "$s" "$(cat host_${s}_bias.txt)" "$(cat qemu_${s}_bias.txt)"
    fail=1
  fi
done
for s in $NEG_SCENARIOS; do
  ../hosttest/test "$s" "$AMOUNT" "$DURATION" hash=1 "range=$NEG_RANGE" \
    "bias_lfo=$BIAS_LFO" > "host_${s}_neg.txt"
  if cmp -s "host_${s}_neg.txt" "qemu_${s}_neg.txt"; then
    printf 'PASS %-10s neg hash %s  (asm == C)\n' "$s" "$(cat qemu_${s}_neg.txt)"
  else
    printf 'FAIL %-10s neg host=%s qemu=%s\n' "$s" "$(cat host_${s}_neg.txt)" "$(cat qemu_${s}_neg.txt)"
    fail=1
  fi
done
[ $fail -eq 0 ] && echo "ALL PASS: render-loop asm is bit-identical to the C reference"
exit $fail
