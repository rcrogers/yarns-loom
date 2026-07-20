// Host driver for the REAL yarns envelope (dart-model port). Renders
// scenarios and dumps per-sample value_q30-scale outputs as text for
// analysis. Usage: ./test <scenario> ; output: one sample (int) per line.
#define private public
#define TEST 1
#include "yarns/envelope.h"
#include "yarns/drivers/dac.h"
#include "stmlib/stmlib.h"
#include "stmlib/utils/dsp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace yarns;
using namespace stmlib;

static Envelope env;
static ADSR adsr;

static uint32_t IncFromSamples(uint32_t samples) {
  return samples ? (UINT32_MAX / samples) : 0;
}

// Front-panel ENV ATTACK/DECAY/RELEASE setting (0..127) -> phase increment,
// via the exact chain Part::VoiceNoteOn uses. Milliseconds cannot express what
// the module actually does: the setting picks a LUT entry, and only 128 stage
// lengths exist.
static uint32_t IncFromSetting(int setting) {
  return stmlib::Interpolate88(
      lut_envelope_phase_increments,
      stmlib::modulate_7_13(static_cast<uint8_t>(setting), 0, 0) << (15 - 13));
}
static uint16_t SustainFromSetting(int setting) {
  return stmlib::modulate_7_13(static_cast<uint8_t>(setting), 0, 0) << (16 - 13);
}

static void RenderMs(double ms, int16_t* out, size_t* pos) {
  size_t n = (size_t)(ms * 45.0);
  for (size_t i = 0; i < n; i += kAudioBlockSize) {
    Envelope::FillSharedPrngBuffer();
    int16_t buffer[kAudioBlockSize];
    env.RenderSamples(buffer, 0);
    for (size_t j = 0; j < kAudioBlockSize; ++j) out[(*pos)++] = buffer[j];
  }
}

// Optional named overrides (KEY=VALUE argv entries) so a sweep can reproduce
// the user's actual front-panel knobs instead of the harness's stock ADSR.
// Positional args stay as they were: scenario amount duration [attack_ms].
static int OptInt(int argc, char** argv, const char* key, int fallback) {
  size_t n = strlen(key);
  for (int i = 1; i < argc; ++i) {
    if (!strncmp(argv[i], key, n) && argv[i][n] == '=') return atoi(argv[i] + n + 1);
  }
  return fallback;
}

int main(int argc, char** argv) {
  const char* scenario = argc > 1 ? argv[1] : "basic";
  uint8_t amount = argc > 2 ? atoi(argv[2]) : 96;
  uint8_t duration = argc > 3 ? atoi(argv[3]) : 90;

  int peak_pct = OptInt(argc, argv, "peak", 100);
  int sustain_pct = OptInt(argc, argv, "sustain", 60);
  adsr.peak_u16 = static_cast<uint16_t>(65535L * peak_pct / 100);
  adsr.sustain_u16 = static_cast<uint16_t>(65535L * sustain_pct / 100);
  int atkms = argc > 4 && strchr(argv[4], '=') == NULL ? atoi(argv[4]) : 1200;
  atkms = OptInt(argc, argv, "attack", atkms);
  int decms = OptInt(argc, argv, "decay", 300);
  int relms = OptInt(argc, argv, "release", 400);
  int gatems = OptInt(argc, argv, "gate", 2000);
  int max_target = OptInt(argc, argv, "range", 16383);
  adsr.attack_u32 = IncFromSamples(atkms * 45);
  adsr.decay_u32 = IncFromSamples(decms * 45);
  adsr.release_u32 = IncFromSamples(relms * 45);
  // Front-panel SETTINGS (0..127) override the millisecond forms. These are
  // what the module actually exposes; prefer them.
  int atk_set = OptInt(argc, argv, "attack_setting", -1);
  int dec_set = OptInt(argc, argv, "decay_setting", -1);
  int rel_set = OptInt(argc, argv, "release_setting", -1);
  int sus_set = OptInt(argc, argv, "sustain_setting", -1);
  if (atk_set >= 0) adsr.attack_u32 = IncFromSetting(atk_set);
  if (dec_set >= 0) adsr.decay_u32 = IncFromSetting(dec_set);
  if (rel_set >= 0) adsr.release_u32 = IncFromSetting(rel_set);
  if (sus_set >= 0) adsr.sustain_u16 = SustainFromSetting(sus_set);
  if (OptInt(argc, argv, "report", 0)) {
    fprintf(stderr, "attack %u smp, decay %u smp, release %u smp, chiff %u smp\n",
            adsr.attack_u32 ? UINT32_MAX / adsr.attack_u32 : 0,
            adsr.decay_u32 ? UINT32_MAX / adsr.decay_u32 : 0,
            adsr.release_u32 ? UINT32_MAX / adsr.release_u32 : 0,
            lut_chiff_duration_samples[duration]);
  }

  static int16_t out[45 * 20000];
  size_t pos = 0;
  env.Init(strcmp(scenario, "inverted") == 0 ? 16383 : 0);  // rest = release level

  if (strcmp(scenario, "basic") == 0) {
    // gate, then release to the end
    env.NoteOn(adsr, 0, max_target, amount, duration);
    RenderMs(gatems, out, &pos);
    env.NoteOff();
    RenderMs(OptInt(argc, argv, "tail", relms > 1000 ? relms + 200 : 1000),
             out, &pos);
  } else if (strcmp(scenario, "early_release") == 0) {
    // release 60ms into the attack
    env.NoteOn(adsr, 0, 16383, amount, duration);
    RenderMs(60, out, &pos);
    env.NoteOff();
    RenderMs(1000, out, &pos);
  } else if (strcmp(scenario, "retrigger") == 0) {
    // note, release, retrigger mid-release
    env.NoteOn(adsr, 0, 16383, amount, duration);
    RenderMs(500, out, &pos);
    env.NoteOff();
    RenderMs(100, out, &pos);
    env.NoteOn(adsr, 0, 16383, amount, duration);
    RenderMs(1500, out, &pos);
  } else if (strcmp(scenario, "inverted") == 0) {
    // Numerically inverted range (CV DAC / negative timbre): min > max
    env.NoteOn(adsr, 16383, 0, amount, duration);
    RenderMs(2000, out, &pos);
    env.NoteOff();
    RenderMs(1000, out, &pos);
  } else if (strcmp(scenario, "latehang") == 0) {
    // Early release near the dark end of a long window: 100ms release at 7s
    // into an 8s chiff. Must fall with the release, not hang.
    adsr.attack_u32 = IncFromSamples(200 * 45);
    adsr.decay_u32 = IncFromSamples(200 * 45);
    adsr.release_u32 = IncFromSamples(100 * 45);
    env.NoteOn(adsr, 0, 16383, amount, duration);
    RenderMs(7000, out, &pos);
    env.NoteOff();
    RenderMs(400, out, &pos);
  } else if (strcmp(scenario, "held") == 0) {
    // long hold: chiff through attack into sustain
    env.NoteOn(adsr, 0, 16383, amount, duration);
    RenderMs(9000, out, &pos);
    env.NoteOff();
    RenderMs(600, out, &pos);
  }
  for (size_t i = 0; i < pos; ++i) printf("%d\n", out[i]);
  return 0;
}
