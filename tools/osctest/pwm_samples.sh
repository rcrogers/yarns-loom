#!/bin/sh
# AUDIO for the audio-rate PWM run, and the shapes worth hearing it against.
#
# One file per case, plus a tour that plays them in order with a gap, because
# auditioning twenty separate files is not auditioning.
#
#   sh tools/osctest/pwm_samples.sh [outdir]
#
# Gain profile 0 is the constant one and `dump` renders both, so each case is
# cut to one profile's worth of samples. `hold=1 warp=1` feeds the warped
# timbre, which is what Voice hands a render -- the raw value would be a tone
# the firmware cannot make.
set -e
cd "$(dirname "$0")/../.."
OUT="${1:-$HOME/Desktop/yarns-pwm-samples}"
mkdir -p "$OUT"

RATE=45000
BLOCKS=1100                     # 70400 samples, 1.56 s
SAMPLES=$((BLOCKS * 64))
GAP=$((RATE / 4))               # silence between tour entries

render() {                      # shape pitch timbre -> stdout, one profile
  ./tools/osctest/osctest dump shape="$1" pitch="$2" timbre="$3" \
    hold=1 warp=1 sweep=4 blocks="$BLOCKS" | head -n "$SAMPLES"
}

write() {                       # shape pitch timbre name
  render "$1" "$2" "$3" > "$OUT/.raw"
  node tools/osctest/wav.js rate="$RATE" < "$OUT/.raw" > "$OUT/$4.wav"
  printf '  %-34s %s\n' "$4.wav" "$(wc -c < "$OUT/$4.wav") bytes"
}

# shape:pitch:timbre:name. The PWM run starts at 71, FM at 45.
CASES="
18:48:0:ref-01_static_pulse_50pct
18:48:32767:ref-02_static_pulse_narrow
50:48:32767:ref-03_FM_5over2
72:48:32767:pwm-01_2over1_integer_static
76:48:32767:pwm-02_5over2
78:48:32767:pwm-03_9over2
79:48:32767:pwm-04_7over3
81:48:32767:pwm-05_9over4
86:48:32767:pwm-06_minkowski_2over5
89:48:32767:pwm-07_minkowski_1over3
90:48:32767:pwm-08_pi_over_4
93:48:32767:pwm-09_pi
94:48:32767:pwm-10_two_pi
90:48:8192:depth-01_pi_over_4_timbre_8192
90:48:16384:depth-02_pi_over_4_timbre_16384
90:48:32767:depth-03_pi_over_4_timbre_32767
90:72:32767:pitch-01_pi_over_4_midi72
90:84:32767:pitch-02_pi_over_4_midi84
94:84:32767:pitch-03_two_pi_midi84
"

echo "writing to $OUT"
for c in $CASES; do
  shape=$(echo "$c" | cut -d: -f1)
  pitch=$(echo "$c" | cut -d: -f2)
  timbre=$(echo "$c" | cut -d: -f3)
  name=$(echo "$c" | cut -d: -f4)
  write "$shape" "$pitch" "$timbre" "$name"
done

# The tour: every ratio at one pitch and one depth, in table order, so the
# families are heard next to each other rather than hunted for.
echo "  building tour..."
: > "$OUT/.tour"
for shape in 71 72 73 74 75 76 77 78 79 80 81 82 83 84 85 86 87 88 89 90 91 92 93 94 95 96; do
  render "$shape" 48 32767 >> "$OUT/.tour"
  i=0
  while [ "$i" -lt "$GAP" ]; do echo 0; i=$((i + 1)); done >> "$OUT/.tour"
done
node tools/osctest/wav.js rate="$RATE" < "$OUT/.tour" > "$OUT/tour_all_26_ratios.wav"
printf '  %-34s %s\n' "tour_all_26_ratios.wav" "$(wc -c < "$OUT/tour_all_26_ratios.wav") bytes"

rm -f "$OUT/.raw" "$OUT/.tour"
echo "done"
