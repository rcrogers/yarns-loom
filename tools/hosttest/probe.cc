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
#include <algorithm>
using namespace yarns;

static Envelope env;
static ADSR adsr;

// Front-panel setting (0..127) -> phase increment, the exact part.cc chain.
static uint32_t IncFromSetting(int setting) {
  return stmlib::Interpolate88(
      lut_envelope_phase_increments,
      stmlib::modulate_7_13(static_cast<uint8_t>(setting), 0, 0) << (15 - 13));
}

// Slew time log2 (Q5.27) and slew rate (Q31) are two encodings of the same
// thing; print both as slew time so they must track.
// They must stay consistent; printing both exposes any desync.
static double SlewTimeLog2Of(uint32_t q5_27) { return q5_27 / 134217728.0; }
static double SlewTimeLog2OfRate(int32_t slew_rate_q31) {
  return slew_rate_q31 > 0 ? -log2(slew_rate_q31 / 2147483648.0) : 99.0;
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
  fprintf(stderr, "attack %u smp, decay %u smp\n",
          UINT32_MAX / adsr.attack_u32, UINT32_MAX / adsr.decay_u32);

  env.Init(0);
  env.NoteOn(adsr, 0, 16383, amount, duration);

  // EXPERIMENT-era columns: the perturbation is derived (shrink x half the
  // allowed range), and what has to reach inaudibility is the OUTPUT it
  // produces -- perturbation x the slew's response at the chiff's OWN rate,
  // which is what the floor's rescale preserves. Printed in dBFS against the
  // note's range so it compares directly with the residual/wander scripts.
  printf("blk stage  chiffLeft  slewTime  rateAsTime  stageRateAsTime"
         "  shrink  perturb  outDbfs  octLeft floorBinds\n");
  const double kFullScale = 16383.0 * 32768.0;
  int16_t buffer[kAudioBlockSize];
  for (int b = 0; b < blocks; ++b) {
    if (b == gate_off_block) env.NoteOff();
    bool floor_binds = env.phase_increment_u32_ != 0 &&
                       env.slew_rate_q31_ < env.stage_slew_rate_q31_;
    if (b >= from_block) {
      const double rate = env.slew_rate_q31_ / 2147483648.0;
      const double response = std::min(1.0, 3.0 * sqrt(rate / (2.0 * (2.0 - rate))));
      const double perturb = env.ChiffPerturb_q30();
      const double out = perturb * response;
      const double oct_left = env.chiff_perturb_shrink_step_q5_27_
        * (double)env.chiff_duration_samples_left_ / 134217728.0;
      printf("%3d %5d %10u %8.3f %11.3f %15.3f %8.5f %9.0f %8.1f %8.2f %s\n",
             b, (int)env.stage_, env.chiff_duration_samples_left_,
             SlewTimeLog2Of(env.slew_time_log2_q5_27_),
             SlewTimeLog2OfRate(env.slew_rate_q31_),
             SlewTimeLog2OfRate(env.stage_slew_rate_q31_),
             env.chiff_perturb_shrink_q30_ / 1073741824.0,
             perturb,
             out > 0 ? 20 * log10(out / kFullScale) : -999.0,
             oct_left,
             floor_binds ? "FLOOR" : "");
    }
    Envelope::FillSharedPrngBuffer();
    env.RenderSamples(buffer, 0);
  }
  return 0;
}
