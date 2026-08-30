// Host driver for the OSCILLATOR SHAPES. Renders each shape's own function
// with a CONTROLLED timbre and gain buffer, not through Envelope::RenderSamples
// -- so what is pinned is the shape's arithmetic and nothing else. That is the
// point: the per-sample timbre is 15 bits today, and widening it means ten
// rescaled sites, each reading that value as a different quantity (a
// multiplier, a cutoff, a phase increment, a zone index). Feeding the buffer
// directly is what makes "same audio from the equivalent wider value" a
// checkable claim.
//
// Nothing rendered an oscillator sample off target before this.
//
// Usage: ./osctest <mode> [KEY=VALUE ...]
//   hash              one FNV-1a per shape, over every rendered sample
//   dump shape=<n>    the samples themselves
#define TEST 1
#define private public
#include "yarns/oscillator.h"
#include "yarns/drivers/dac.h"
#include "stmlib/utils/random.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace yarns;

namespace {

Oscillator osc;

// The scale Init is given, and the pitches and timbre span every case walks.
// Three pitches so a shape whose warp tracks pitch is exercised at more than
// one, and a timbre RAMP so the per-sample path moves rather than sitting.
const uint16_t kScale = 32767;
const int kPitches[] = { 36 << 7, 60 << 7, 96 << 7 };
const int kBlocks = 8;

// Full-scale timbre at the CURRENT width. A wider one must render the same
// audio from the proportionally larger value, which is what the check asserts.
int g_timbre_max = 32767;
int g_gain = 32767;

uint32_t Fnv(uint32_t h, int16_t v) {
  return (h ^ static_cast<uint16_t>(v)) * 16777619u;
}

uint32_t HashShape(int shape, bool dump) {
  uint32_t hash = 2166136261u;
  // Per shape, so a noise shape's hash does not depend on how many draws the
  // shapes before it took.
  stmlib::Random::Seed(0x21);
  for (size_t p = 0; p < sizeof(kPitches) / sizeof(kPitches[0]); ++p) {
    osc.Init(kScale);
    osc.set_shape(static_cast<OscillatorShape>(shape));
    osc.Refresh(static_cast<int16_t>(kPitches[p]), 0, 0);
    for (int b = 0; b < kBlocks; ++b) {
      int16_t timbre_gain[2 * kAudioBlockSize];
      int16_t mix[kAudioBlockSize];
      memset(mix, 0, sizeof(mix));
      for (size_t i = 0; i < kAudioBlockSize; ++i) {
        // A ramp across the whole run, so every shape sees its timbre move.
        const long step = b * kAudioBlockSize + i;
        timbre_gain[i] = static_cast<int16_t>(
            g_timbre_max * step / (kBlocks * kAudioBlockSize));
        timbre_gain[i + kAudioBlockSize] = static_cast<int16_t>(g_gain);
      }
      (osc.*Oscillator::fn_table_[shape])(timbre_gain, mix);
      for (size_t i = 0; i < kAudioBlockSize; ++i) {
        hash = Fnv(hash, mix[i]);
        if (dump) printf("%d\n", mix[i]);
      }
    }
  }
  return hash;
}

int OptInt(int argc, char** argv, const char* key, int fallback) {
  size_t n = strlen(key);
  for (int i = 1; i < argc; ++i) {
    if (!strncmp(argv[i], key, n) && argv[i][n] == '=') return atoi(argv[i] + n + 1);
  }
  return fallback;
}

}  // namespace

int main(int argc, char** argv) {
  const char* mode = argc > 1 ? argv[1] : "hash";
  g_timbre_max = OptInt(argc, argv, "timbre_max", 32767);
  g_gain = OptInt(argc, argv, "gain", 32767);

  if (!strcmp(mode, "dump")) {
    HashShape(OptInt(argc, argv, "shape", 0), true);
    return 0;
  }
  for (int s = 0; s <= OSC_SHAPE_FM; ++s) {
    printf("%d %08x\n", s, HashShape(s, false));
  }
  return 0;
}
