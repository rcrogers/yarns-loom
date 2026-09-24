#!/bin/sh
# PROVE A CHANGE IS BEHAVIOUR-NEUTRAL, against the tree as it stands rather than
# against the recorded goldens.
#
# `golden.js` answers "did this move from what we shipped", which is the wrong
# question mid-optimisation: once a deliberate change has moved the goldens,
# every later refactor fails it for the same stale reason and the signal is
# gone. This answers "did THIS change move anything", which is the question a
# behaviour-neutral optimisation has to pass.
#
#   sh tools/hosttest/bitexact.sh record    before the change
#   sh tools/hosttest/bitexact.sh check     after it
set -e
cd "$(dirname "$0")"
REF=/tmp/yarns_bitexact_ref.txt
OUT=$(mktemp)
python3 ../portable_envelope.py ../.. envelope_host.cc
clang++ -std=c++11 -O1 -w -DTEST -I shim -I ../.. \
  envelope_host.cc ../../yarns/resources.cc ../../yarns/utils.cc driver.cc -o test
# The scenarios the driver offers, at settings that put the chiff across its
# whole range -- and seeds, because one realization is not a result.
for scenario in basic early_release retrigger chiff_then_off inverted latehang held; do
  for amount in 0 3 40 96 127; do
    for duration in 0 6 16 40 90 127; do
      for seed in 0 1; do
        printf '%s %s %s %s ' "$scenario" "$amount" "$duration" "$seed" >> "$OUT"
        ./test "$scenario" "$amount" "$duration" attack_setting=40 seed="$seed" \
          | cksum >> "$OUT"
      done
    done
  done
done
case "$1" in
  record) mv "$OUT" "$REF"; echo "recorded $(wc -l < "$REF") cases to $REF" ;;
  check)
    if diff -q "$REF" "$OUT" > /dev/null; then
      echo "BIT-EXACT: all $(wc -l < "$OUT") cases identical to the reference"
      rm -f "$OUT"
    else
      echo "MOVED: $(diff "$REF" "$OUT" | grep -c '^<') of $(wc -l < "$OUT") cases"
      diff "$REF" "$OUT" | head -10
      rm -f "$OUT"; exit 1
    fi ;;
  *) echo "usage: bitexact.sh record|check"; rm -f "$OUT"; exit 1 ;;
esac
