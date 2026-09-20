// THE MIX MUST STAY INSIDE THE OUTPUT VOLTAGE RANGE, for every shape, at every
// voice count, whatever the panel asks for. voice.h hands each voice scale_,
// and scale_ is what one of them may put on the output.
//
// This is the contract nothing enforced. Each voice is given a share of the
// range and every shape ends in a clip, so the arithmetic works out ONLY while
// each shape stays inside its share -- and the mix accumulator is an int16
// carrying a uint16 DAC code, so a shape that does not is not clipped, it
// WRAPS: the code passes 65535 and the output jumps from -5 V to +7 V. Six
// shapes broke the contract silently the moment the range doubled (5f739a20),
// because the shapes the gain envelope EXCITES turn the chiff's overdrive into
// output past their share.
//
// So the check is at the DAC, on the real path: Voice::NoteOn through
// CVOutput::RenderSamples into the stub, reading the codes the hardware would
// have been handed. A wrap lands far outside the window on the opposite side,
// so one bounds check catches both a wrap and a plain overshoot.
//
// WHAT IT CANNOT DO: bound a NOISE peak. Those grow as sqrt(2 ln N) with how
// long you listen, so a passing run says the shape is inside its share over
// THIS run, not for ever. WHISTLE is the one shape whose bound is arithmetic
// rather than statistical -- it caps bp against its share in the render -- and
// that is why the cap stays.
//
// Usage: ./mixtest [verbose]
//        ./mixtest dump shape=N pitch=96 knob=0 vb=10 vr=1 lfo_ms=5000 blocks=2100
//
// THE DUMP IS THE ONLY RENDER IN THIS REPO WITH THE PITCH LFO ACTUALLY RUNNING.
// `osctest` writes the timbre and gain buffers itself and calls
// Oscillator::Refresh with a pitch the harness chose, so no measurement taken
// there contains a vibrato -- and the CZ artifact the user reports is audible
// ONLY with vibrato on. This walks the real path at the real clocks.
#define TEST 1
#define private public
#include "yarns/voice.h"
#include "tools/cvtest/dac_stub.h"
#include "tools/panel_chain.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace stmlib { uint32_t Random::rng_state_ = 0x21; }

using namespace yarns;
using namespace stmlib;

namespace {

const uint8_t kMaxVoices = 4;
const uint8_t kChannel = 0;

Voice voices[kMaxVoices];
CVOutput audio_output;
ADSR adsr;

// The corners a shape can be driven into. Pitch and TIMBRE span their settings;
// the chiff is swept because it is the exciter's overdrive, and it is what six
// shapes turned into output past their share.
const int kPitches[] = { 24, 48, 60, 84, 108 };
const int kTimbres[] = { 0, 8192, 16384, 32767 };
const int kChiffAmounts[] = { 0, 64, 127 };
// The panel knob, which sets where a shape's map starts before the envelope
// moves it. WHISTLE's level spans 24 dB across this.
const int kKnobs[] = { 0, 64, 127 };
// Long enough for the strike and the ring after it; the shapes that break the
// contract do it at the onset.
const int kBlocks = 45000 / 4 / kAudioBlockSize;

struct Worst {
  int32_t excursion;
  int shape, pitch, timbre, chiff, voices, knob;
};

int OptInt(int argc, char** argv, const char* key, int fallback) {
  size_t n = strlen(key);
  for (int i = 1; i < argc; ++i) {
    if (!strncmp(argv[i], key, n) && argv[i][n] == '=') return atoi(argv[i] + n + 1);
  }
  return fallback;
}

int32_t Excursion(int16_t sample, uint16_t zero_code) {
  // The buffer holds a uint16 DAC code; the excursion is its distance from the
  // 0 V code, in codes. Signed 16-bit arithmetic on the difference is exact for
  // anything the DAC can carry.
  return static_cast<int16_t>(static_cast<uint16_t>(sample) - zero_code);
}

void RunCase(int shape, int num_voices, int pitch, int timbre, int chiff,
             int timbre_knob, Worst* worst) {
  Random::Seed(0x21);
  for (uint8_t v = 0; v < kMaxVoices; ++v) voices[v].Init();
  audio_output.Init(true);
  for (uint8_t v = 0; v < num_voices; ++v) {
    voices[v].set_oscillator_mode(OSCILLATOR_MODE_ENVELOPED);
    voices[v].set_oscillator_shape(static_cast<uint8_t>(shape));
    voices[v].set_timbre_init(static_cast<uint8_t>(timbre_knob));
    voices[v].timbre_init_current_ = voices[v].timbre_init_target_;
  }
  audio_output.AssignVoices(&voices[0], DC_PITCH,
                            num_voices, num_voices);

  adsr.peak_u16 = UINT16_MAX;
  adsr.sustain_u16 = static_cast<uint16_t>(65535L * 60 / 100);
  adsr.attack_u32 = PanelStageIncrement(0, 0, 0);
  adsr.decay_u32 = PanelStageIncrement(64, 0, 0);
  adsr.release_u32 = PanelStageIncrement(64, 0, 0);
  const uint32_t chiff_amount_q30 = PanelChiffAmount_q30(chiff, 0, 0);
  const uint32_t chiff_audible_samples = PanelChiffAudibleSamples(67, 0, 0);

  // A chord rather than a unison, so the voices are not one waveform times n.
  const int kIntervals[] = { 0, 7, 12, 16 };
  for (uint8_t v = 0; v < num_voices; ++v) {
    voices[v].NoteOn(static_cast<int16_t>((pitch + kIntervals[v]) << 7), 100,
                     0, 0, true, adsr, static_cast<int16_t>(timbre),
                     chiff_amount_q30, chiff_audible_samples);
  }

  const uint16_t zero_code = audio_output.zero_dac_code_;
  for (int block = 0; block < kBlocks; ++block) {
    for (uint8_t v = 0; v < num_voices; ++v) voices[v].Refresh();
    audio_output.RenderSamples(0, kChannel, 0);
    for (size_t i = 0; i < kAudioBlockSize; ++i) {
      int32_t e = Excursion(g_dac_block[kChannel][i], zero_code);
      if (e < 0) e = -e;
      if (e > worst->excursion) {
        worst->excursion = e;
        worst->shape = shape; worst->pitch = pitch; worst->timbre = timbre;
        worst->chiff = chiff; worst->voices = num_voices;
        worst->knob = timbre_knob;
      }
    }
  }
}

// ONE VOICE, RENDERED WITH ITS MODULATION LIVE, to stdout as signed samples.
//
// THE TWO CLOCKS ARE ASYNCHRONOUS AND THAT IS THE POINT. Voice::Refresh runs at
// kRefreshHz and the render at 45000/64, which is 5.6889 refreshes a block --
// not an integer. Refreshing once a block instead would clock the LFO 5.7x
// slow, so a vibrato would come out at the wrong rate and any artifact that
// depends on how FAST the pitch moves would be measured at the wrong speed.
int DumpOneVoice(int argc, char** argv) {
  const int shape = OptInt(argc, argv, "shape", OSC_SHAPE_CZ_PULSE_LP);
  // In the pitch pipeline's own units, 128 a semitone, because the artifact's
  // DC-crossing windows are tens of cents apart and a semitone grid steps over
  // them. `pitch=` names the semitone.
  const int pitch_raw =
      OptInt(argc, argv, "pitch_raw", OptInt(argc, argv, "pitch", 96) << 7);
  const int knob = OptInt(argc, argv, "knob", 0);
  const int timbre_mod = OptInt(argc, argv, "timbre", 0);
  const int vibrato_mod = OptInt(argc, argv, "vb", 10);
  const int vibrato_range = OptInt(argc, argv, "vr", 1);
  // The PERIOD, in ms, because the rate that matters here is slow: a vibrato of
  // several seconds is what makes the pitch's own steps audible as zones, and a
  // fast one hides them by never dwelling.
  const int lfo_ms = OptInt(argc, argv, "lfo_ms", 5000);
  const int lfo_shape = OptInt(argc, argv, "lfo_shape", 0);
  const int blocks = OptInt(argc, argv, "blocks", 2100);
  const int chiff = OptInt(argc, argv, "chiff", 0);

  Random::Seed(0x21);
  voices[0].Init();
  audio_output.Init(true);
  voices[0].set_oscillator_mode(OSCILLATOR_MODE_ENVELOPED);
  voices[0].set_oscillator_shape(static_cast<uint8_t>(shape));
  voices[0].set_timbre_init(static_cast<uint8_t>(knob));
  voices[0].timbre_init_current_ = voices[0].timbre_init_target_;
  voices[0].set_vibrato_range(static_cast<uint8_t>(vibrato_range));
  voices[0].set_vibrato_mod(static_cast<uint8_t>(vibrato_mod));
  voices[0].set_lfo_shape(LFO_ROLE_PITCH, static_cast<uint8_t>(lfo_shape));
  // Part sets this from the LFO RATE setting; named in Hz here so a vibrato can
  // be asked for directly.
  voices[0].lfo(LFO_ROLE_PITCH)->SetPhaseIncrement(static_cast<uint32_t>(
      4294967296.0 * 1000.0 / (static_cast<double>(lfo_ms) * kRefreshHz)));
  audio_output.AssignVoices(&voices[0], DC_PITCH, 1, 1);

  adsr.peak_u16 = UINT16_MAX;
  adsr.sustain_u16 = static_cast<uint16_t>(65535L * 60 / 100);
  adsr.attack_u32 = PanelStageIncrement(0, 0, 0);
  adsr.decay_u32 = PanelStageIncrement(127, 0, 0);
  adsr.release_u32 = PanelStageIncrement(64, 0, 0);
  voices[0].NoteOn(static_cast<int16_t>(pitch_raw), 100, 0, 0, true, adsr,
                   static_cast<int16_t>(timbre_mod),
                   PanelChiffAmount_q30(chiff, 0, 0),
                   PanelChiffAudibleSamples(67, 0, 0));

  const uint16_t zero_code = audio_output.zero_dac_code_;
  double refreshes_owed = 0.0;
  for (int block = 0; block < blocks; ++block) {
    refreshes_owed +=
        static_cast<double>(kAudioBlockSize) * kRefreshHz / 45000.0;
    while (refreshes_owed >= 1.0) { voices[0].Refresh(); refreshes_owed -= 1.0; }
    audio_output.RenderSamples(0, kChannel, 0);
    for (size_t i = 0; i < kAudioBlockSize; ++i) {
      printf("%d\n", Excursion(g_dac_block[kChannel][i], zero_code));
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && !strcmp(argv[1], "dump")) return DumpOneVoice(argc, argv);
  const bool verbose = argc > 1 && !strcmp(argv[1], "verbose");

  // THE ALLOWANCE IS READ BACK FROM voice.h, not restated here: one voice is
  // given the whole of it, and scale_ is what its envelope peaks at.
  // Stating that twice is how the check and the thing checked drift apart.
  voices[0].Init();
  audio_output.Init(true);
  audio_output.AssignVoices(&voices[0], DC_PITCH, 1, 1);
  const int32_t allowance = voices[0].oscillator()->coherent_scale_codes_u16_;

  int failures = 0;
  for (int shape = 0; shape < kOscShapeLast; ++shape) {
    Worst worst; memset(&worst, 0, sizeof(worst));
    for (int n = 1; n <= kMaxVoices; ++n) {
      for (size_t p = 0; p < sizeof(kPitches)/sizeof(kPitches[0]); ++p) {
        for (size_t t = 0; t < sizeof(kTimbres)/sizeof(kTimbres[0]); ++t) {
          for (size_t c = 0; c < sizeof(kChiffAmounts)/sizeof(kChiffAmounts[0]); ++c)
          for (size_t k = 0; k < sizeof(kKnobs)/sizeof(kKnobs[0]); ++k) {
            RunCase(shape, n, kPitches[p], kTimbres[t], kChiffAmounts[c],
                    kKnobs[k], &worst);
          }
        }
      }
    }
    const bool over = worst.excursion > allowance;
    if (over) ++failures;
    if (over || verbose) {
      printf("%s shape %2d  worst %6d of %d (%.2f)  "
             "MIDI %d, TIMBRE %d + mod %d, EXCITER %d, %d voice(s)\n",
             over ? "FAIL" : "    ", shape, worst.excursion, allowance,
             worst.excursion / (double) allowance,
             worst.pitch, worst.knob, worst.timbre, worst.chiff, worst.voices);
    }
  }
  if (failures) {
    printf("\n%d shape(s) leave the output range the voices were given. Past it\n"
           "the DAC code wraps and the output inverts; there is no clipping in\n"
           "between.\n", failures);
    return 1;
  }
  printf("PASS %d shapes stay inside the output range at 1..%d voices "
         "(%lu cases each)\n",
         kOscShapeLast, kMaxVoices,
         (unsigned long) (kMaxVoices * 5 * 4 * 3 * 3));
  return 0;
}
