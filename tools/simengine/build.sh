#!/bin/sh
# Compile the REAL yarns/envelope.cc to a self-contained JS engine for
# chiff_sim.html. One source of truth: the sim runs firmware code.
#
# WASM=0 emits plain JavaScript (asm.js), not a binary. That keeps the whole
# engine inlineable into a published artifact with no separate fetch and no
# reliance on the page's CSP permitting WebAssembly compilation.
set -e
cd "$(dirname "$0")"
ROOT=$(cd ../.. && pwd)

python3 ../portable_envelope.py "$ROOT" envelope_portable.cc

docker run --rm -v "$ROOT:/src" -w /src/tools/simengine \
  emscripten/emsdk:latest \
  em++ -O2 -std=c++11 -w -DTEST \
    -I ../hosttest/shim -I /src \
    engine.cc /src/yarns/resources.cc \
    -o chiff_engine.js \
    -s WASM=0 \
    -s SINGLE_FILE=1 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME=ChiffEngine \
    -s ENVIRONMENT=web,node \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s INVOKE_RUN=0 \
    -s EXPORTED_FUNCTIONS='["_chiff_render","_chiff_meta_count","_chiff_frame_hz","_chiff_duration_samples","_chiff_stage_samples","_malloc","_free"]' \
    -s EXPORTED_RUNTIME_METHODS='["cwrap","HEAP16","HEAP32"]'

echo "built $(pwd)/chiff_engine.js ($(wc -c < chiff_engine.js) bytes)"
