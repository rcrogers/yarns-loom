// State probe: dump the chiff's per-block internal state so sim-vs-firmware
// divergences in the STATE (not just the output) are visible. Built by
// build.sh alongside the main driver.
#define private public
#define TEST 1
#include "yarns/envelope.h"
#include "yarns/drivers/dac.h"
#include "stmlib/stmlib.h"
#include "stmlib/utils/dsp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
using namespace yarns;

static Envelope env;
static ADSR adsr;

// Front-panel setting (0..127) -> phase increment, the exact part.cc chain.
static uint32_t IncFromSetting(int setting) {
  return stmlib::Interpolate88(
      lut_envelope_phase_increments,
      stmlib::modulate_7_13(static_cast<uint8_t>(setting), 0, 0) << (15 - 13));
}

// shift (Q5.27) and alpha (Q31) are two encodings of the same slew rate.
// They must stay consistent; printing both exposes any desync.
static double ShiftOf(uint32_t q5_27) { return q5_27 / 134217728.0; }
static double AlphaShift(int32_t alpha_q31) {
  return alpha_q31 > 0 ? -log2(alpha_q31 / 2147483648.0) : 99.0;
}

int main(int argc, char** argv) {
  uint8_t amount = argc > 1 ? atoi(argv[1]) : 96;
  uint8_t duration = argc > 2 ? atoi(argv[2]) : 90;
  int attack_setting = argc > 3 ? atoi(argv[3]) : 40;
  int blocks = argc > 4 ? atoi(argv[4]) : 40;
  int decay_setting = argc > 5 ? atoi(argv[5]) : 64;
  int release_setting = argc > 6 ? atoi(argv[6]) : 64;
  // Block index at which to NoteOff; -1 = never (stay gated).
  int gate_off_block = argc > 7 ? atoi(argv[7]) : -1;
  // Print only from this block on, so a long pre-roll doesn't flood output.
  int from_block = argc > 8 ? atoi(argv[8]) : 0;

  adsr.peak_u16 = 49151;   // 75%
  adsr.sustain_u16 = 36044; // 55%
  adsr.attack_u32 = IncFromSetting(attack_setting);
  adsr.decay_u32 = IncFromSetting(decay_setting);
  adsr.release_u32 = IncFromSetting(release_setting);
  fprintf(stderr, "attack %u smp, decay %u smp, chiff %u smp\n",
          UINT32_MAX / adsr.attack_u32, UINT32_MAX / adsr.decay_u32,
          lut_chiff_duration_samples[duration]);

  env.Init(0);
  env.NoteOn(adsr, 0, 16383, amount, duration);

  printf("blk stage  chiffLeft  shift  alphaAsShift  dialedAsShift  amp"
         "  ampAsFracOfInitial floorBinds | stageStart  target  value\n");
  const int32_t amp0 = env.chiff_amp_q30_;
  int16_t buffer[kAudioBlockSize];
  for (int b = 0; b < blocks; ++b) {
    if (b == gate_off_block) env.NoteOff();
    bool floor_binds = env.phase_increment_u32_ != 0 &&
                       env.slew_alpha_q31_ < env.dialed_alpha_q31_;
    if (b >= from_block) {
      printf("%3d %5d %10u %6.3f %13.3f %14.3f %11d %8.4f %s\n",
             b, (int)env.stage_, env.chiff_duration_samples_left_,
             ShiftOf(env.slew_shift_q5_27_),
             AlphaShift(env.slew_alpha_q31_),
             AlphaShift(env.dialed_alpha_q31_),
             env.chiff_amp_q30_,
             amp0 ? (double)env.chiff_amp_q30_ / amp0 : 0.0,
             floor_binds ? "FLOOR" : "");
      printf("      -> stageStart %11d  target %11d  value %11d\n",
             env.stage_start_q30_, env.target_q30_, env.value_q30_);
    }
    Envelope::FillSharedPrngBuffer();
    env.RenderSamples(buffer, 0);
  }
  return 0;
}
