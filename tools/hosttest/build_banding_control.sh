#!/bin/sh
# Build a host binary with BOTH rail protections stripped, so banding.js can be
# asked the only question that matters about it: does it discriminate?
#
# banding.js counts spectrogram columns whose broadband energy sits below their
# neighbours. On the real firmware that is 0.382% of live columns, and nobody
# knows whether that is a lot. Three earlier attempts at an automatic banding
# metric were abandoned for want of a case that visibly bands -- rail-dwell run
# length reads 1-6 samples everywhere, guard or no guard.
#
# The two protections, in the current design:
#   - the mean's rail correction, which holds the mean a clip threshold inside
#     each rail so `mean + chiff` cannot reach one (OffsetForChiffAmplitude)
#   - the saturating clip on the slew state, which stops the chiff carrying more
#     than it is allowed to show (chiff_clip_threshold_q26)
#
# Stripping both should put the value on the rails constantly. Everything else
# is the real envelope, compiled from the real source.
#
# The predecessor of this script patched `dart_mask`, `chiff_draw_u32` and
# `chiff_fit_at_floor_`, none of which have existed for some time -- it failed
# on its own assertion. Anchors are asserted here for the same reason, so this
# one dies loudly too rather than silently building the real firmware and
# reporting it as the control.
set -e
cd "$(dirname "$0")"
python3 ../portable_envelope.py ../.. envelope_banding_control.cc
python3 - <<'PYEOF'
path = 'envelope_banding_control.cc'
src = open(path).read()

mean_old = '''  if (mean_q29 < min_q29) {
    return (static_cast<uint32_t>(min_q29) - static_cast<uint32_t>(mean_q29)) << 1;
  }'''
mean_new = '''  if (false) {  // BANDING CONTROL: no rail correction on the mean
    return (static_cast<uint32_t>(min_q29) - static_cast<uint32_t>(mean_q29)) << 1;
  }'''
assert mean_old in src, 'rail-correction anchor moved'
src = src.replace(mean_old, mean_new, 1)

clip_old = '    const int32_t chiff_clip_threshold_q26 = chiff_clip_threshold_q30'
clip_new = ('    const int32_t chiff_clip_threshold_q26 = INT32_MAX >> 1;  '
            '// BANDING CONTROL: no clip\n    const int32_t unused_clip_q26 = chiff_clip_threshold_q30')
assert clip_old in src, 'clip-threshold anchor moved'
src = src.replace(clip_old, clip_new, 1)

open(path, 'w').write(src)
print('patched: rail correction and slew clip removed')
PYEOF
clang++ -std=c++11 -O1 -w -DTEST -I shim -I ../.. \
  envelope_banding_control.cc ../../yarns/resources.cc ../../yarns/utils.cc driver.cc \
  -o test_banding_control
echo "built $(pwd)/test_banding_control"
