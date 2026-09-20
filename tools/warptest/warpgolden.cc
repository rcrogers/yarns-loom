// THE WARP'S VALUES, not just its shape.
//
// warpcheck.cc asserts the map is MONOTONE, which every sane map is -- so a
// change that rescales a whole map passes it in silence. That is not
// hypothetical: the CZ map was rescaled onto the room above the note and
// neither warpcheck nor the oscillator golden moved, because `make osc` renders
// with warp=0 and never calls WarpTimbre at all.
//
// This pins what each shape's warp ANSWERS, over the signed input range and at
// the pitches that change the answer, so a map cannot move unnoticed.
#define TEST 1
#define private public
#include "yarns/oscillator.h"
#include "yarns/drivers/dac.h"
#include <cstdio>
#include <cstring>
using namespace yarns;

namespace {

// Past the top and below zero as well: NoteOn warps against a target pitch it
// has NOT clamped, so a warp that uses the raw pitch in an arithmetic it needs
// bounded fails only here. One did -- the CZ map's room went negative and the
// modulator came back 22 cents sharp.
const int16_t kPitches[] = { -5000, -1, 0, 24 << 7, 48 << 7, 60 << 7, 84 << 7,
                             96 << 7, 108 << 7, (128 << 7) - 1, 128 << 7,
                             20000, 32767 };

uint32_t Fnv(uint32_t h, int16_t v) {
  return (h ^ static_cast<uint16_t>(v)) * 16777619u;
}

}  // namespace

int main(int argc, char** argv) {
  Oscillator osc;
  // WarpTimbre reads no scale, so what Init is handed cannot reach it.
  osc.Init(0, 0);
  for (int shape = 0; shape < kOscShapeLast; ++shape) {
    uint32_t hash = 2166136261u;
    for (size_t p = 0; p < sizeof(kPitches) / sizeof(kPitches[0]); ++p) {
      const int16_t pitch = kPitches[p];
      osc.Refresh(pitch, 0, 0);
      // The whole signed range a TIMBRE MOD ENVELOPE can reach, not just the
      // knob's, because NoteOn warps a destination only constrained to int16.
      for (int32_t t = -32768; t <= 32767; t += 97) {
        hash = Fnv(hash, osc.WarpTimbre(static_cast<int16_t>(t),
                                        static_cast<OscillatorShape>(shape),
                                        pitch));
      }
    }
    printf("%d %08x\n", shape, hash);
  }
  return 0;
}
