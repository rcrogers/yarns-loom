#!/bin/sh
# Build a DIAGNOSTIC sim that should show rail banding, for calibrating a
# banding metric against human eyes.
#
# Three attempts to measure banding automatically have failed: rail-dwell run
# length does not discriminate (1-6 samples everywhere, even with the guard
# deleted), and a spectral dip count reads the same for the real firmware and
# for a build with both rail protections removed. Without a case that visibly
# bands there is nothing to calibrate against.
#
# This strips BOTH rail protections from the real envelope:
#   - the rail guard, so the aim centre no longer backs off the peak rail
#   - the relax aim, so every draw is a full-depth dart
# The value should therefore hit the rails constantly. Everything else is the
# actual firmware.
#
# Output: chiff_sim_banding_control.html at the repo root. NOT the real sim --
# do not publish it over chiff_sim.html.
set -e
cd "$(dirname "$0")"
ROOT=$(cd ../.. && pwd)

python3 ../portable_envelope.py "$ROOT" envelope_portable.cc
python3 - <<'PYEOF'
src = open('envelope_portable.cc').read()

relax_old = '      int32_t dart_mask = static_cast<int32_t>(chiff_draw_u32 << 16) >> 31;'
relax_new = '      int32_t dart_mask = -1;  // BANDING CONTROL: no relax aim'
assert relax_old in src, 'relax aim anchor moved'
src = src.replace(relax_old, relax_new, 1)

guard_old = '''    int32_t center_q30 = base_q30;
    if (chiff_fit_at_floor_) {'''
guard_new = '''    int32_t center_q30 = base_q30;
    if (false) if (chiff_fit_at_floor_) {  // BANDING CONTROL: no rail guard'''
assert guard_old in src, 'rail guard anchor moved'
src = src.replace(guard_old, guard_new, 1)

open('envelope_portable.cc', 'w').write(src)
print('patched: rail guard and relax aim removed')
PYEOF

docker run --rm -v "$ROOT:/src" -w /src/tools/simengine \
  emscripten/emsdk:latest \
  em++ -O2 -std=c++11 -w -DTEST \
    -I ../hosttest/shim -I /src \
    engine.cc /src/yarns/resources.cc \
    -o chiff_engine_control.js \
    -s WASM=0 -s SINGLE_FILE=1 -s MODULARIZE=1 -s EXPORT_NAME=ChiffEngine \
    -s ENVIRONMENT=web,node -s ALLOW_MEMORY_GROWTH=1 -s INVOKE_RUN=0 \
    -s EXPORTED_FUNCTIONS='["_chiff_render","_chiff_meta_count","_chiff_frame_hz","_chiff_duration_samples","_chiff_stage_samples","_malloc","_free"]' \
    -s EXPORTED_RUNTIME_METHODS='["cwrap","HEAP16","HEAP32"]'

python3 - <<'PYEOF'
import re
html = open('../../chiff_sim.html').read()
engine = open('chiff_engine_control.js').read()
blocks = list(re.finditer(r'<script>[\s\S]*?</script>', html))
assert len(blocks) == 2, 'expected engine + sim script blocks'
out = html[:blocks[0].start()] + '<script>\n' + engine + '\n</script>' + html[blocks[0].end():]
out = out.replace('<title>Chiff Envelope Simulator</title>',
                  '<title>Chiff Sim - BANDING CONTROL</title>')
out = out.replace(
  '<p>Runs the compiled <code>yarns/envelope.cc</code> itself &mdash; no separate model</p>',
  '<p><b>Diagnostic build, not the real firmware.</b> The rail guard and the '
  'relax aim are both removed, so the value slams into the note\'s rails. '
  'Everything else is the real <code>yarns/envelope.cc</code>. Use it to find '
  'settings where rail banding is visible.</p>')
open('../../chiff_sim_banding_control.html', 'w').write(out)
print('wrote chiff_sim_banding_control.html')
PYEOF

# Leave the tree holding the REAL engine, not the diagnostic one.
python3 ../portable_envelope.py "$ROOT" envelope_portable.cc
echo "done -- run tools/simengine/build.sh to restore chiff_engine.js if needed"
