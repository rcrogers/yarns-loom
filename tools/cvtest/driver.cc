// Host driver for the CV OUTPUT PATH: Voice::NoteOn through CVOutput to the
// DAC. Nothing else off target reaches it -- the sim mirrors
// Part::VoiceNoteOn and stops one call short, and the envelope harness starts
// at Envelope::NoteOn -- which is the gap `acb66287` shipped through, where
// CVOutput::NoteOn narrowed the chiff increment to eight bits and only the aux
// CV outputs were wrong.
//
// Usage: ./cvtest <mode> [KEY=VALUE ...] ; one record per line.
//   chiff    every envelope this note reached, and the chiff parameters it got
//   dac      the aux CV channel's DAC words, one block per line
#define TEST 1
#define private public
#include "yarns/voice.h"
#include "tools/cvtest/dac_stub.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// stmlib::Random lives in a TU the firmware links and this does not.
namespace stmlib { uint32_t Random::rng_state_ = 0x21; }

using namespace yarns;
using namespace stmlib;

namespace {

// One voice, an audio output and both aux outputs, so a single NoteOn reaches
// every envelope the firmware can route: the oscillator's gain and timbre, and
// one per aux CV.
Voice voice;
CVOutput audio_output, aux_1_output, aux_2_output;
ADSR adsr;

const uint8_t kAux1Channel = 1;

int OptInt(int argc, char** argv, const char* key, int fallback) {
  size_t n = strlen(key);
  for (int i = 1; i < argc; ++i) {
    if (!strncmp(argv[i], key, n) && argv[i][n] == '=') return atoi(argv[i] + n + 1);
  }
  return fallback;
}

// Part::VoiceNoteOn's own chain, for the parameters this path carries. The
// stage settings go through the same modulate_7_13 -> table read.
uint32_t IncrementFromSetting(int setting) {
  return Interpolate88(lut_envelope_phase_increments,
                       modulate_7_13(static_cast<uint8_t>(setting), 0, 0) << (15 - 13));
}

void Wire() {
  voice.Init();
  audio_output.Init(true);
  aux_1_output.Init(true);
  aux_2_output.Init(true);
  voice.set_aux_cv(MOD_AUX_ENVELOPE);
  voice.set_aux_cv_2(MOD_AUX_ENVELOPE);
  voice.set_oscillator_mode(OSCILLATOR_MODE_ENVELOPED);
  audio_output.AssignVoices(&voice, DC_PITCH, 1, 1);
  aux_1_output.AssignVoices(&voice, DC_AUX_1, 1, 0);
  aux_2_output.AssignVoices(&voice, DC_AUX_2, 1, 0);
}

// Chiff state is range-independent by construction, so every envelope this
// note reached must hold the same values whatever its own rails are.
void PrintChiff(const char* name, const Envelope& envelope) {
  printf("%s %u %u %u %u %u %d %u %u\n", name,
         envelope.chiff_amount_initial_q30_,
         envelope.chiff_amount_q30_,
         envelope.chiff_phase_step_q32_,
         envelope.chiff_slew_time_at_amount_zero_q5_27_,
         envelope.chiff_slew_time_log2_q5_27_,
         envelope.chiff_slew_input_fraction_q30_,
         envelope.stage_phase_increment_u32_,
         envelope.stage_samples_left_);
}

}  // namespace

int main(int argc, char** argv) {
  const char* mode = argc > 1 ? argv[1] : "chiff";

  const int amount = OptInt(argc, argv, "amount", 96);
  const int duration = OptInt(argc, argv, "duration", 67);
  const int velocity = OptInt(argc, argv, "velocity", 100);
  const int attack_setting = OptInt(argc, argv, "attack_setting", 40);
  const int decay_setting = OptInt(argc, argv, "decay_setting", 64);
  const int release_setting = OptInt(argc, argv, "release_setting", 64);
  const int blocks = OptInt(argc, argv, "blocks", 32);
  const int timbre = OptInt(argc, argv, "timbre", 8192);

  Wire();

  adsr.peak_u16 = UINT16_MAX;
  adsr.sustain_u16 = static_cast<uint16_t>(65535L * 60 / 100);
  adsr.attack_u32 = IncrementFromSetting(attack_setting);
  adsr.decay_u32 = IncrementFromSetting(decay_setting);
  adsr.release_u32 = IncrementFromSetting(release_setting);

  // The two the CV path is here to carry. Part::VoiceNoteOn forms the amount as
  // a fraction of full scale and the duration as an absolute sample count off
  // its own table -- both wider than a byte, which is the whole point.
  const uint32_t chiff_amount_q30 =
      static_cast<uint32_t>((static_cast<uint64_t>(amount) << 30) / 127);
  const uint32_t chiff_audible_samples = ChiffAudibleSamples(Interpolate88(
      lut_chiff_phase_increments, static_cast<uint16_t>(duration) << (15 - 7)));

  voice.NoteOn(60 << 7, static_cast<uint8_t>(velocity), 0, 0, true,
               adsr, static_cast<int16_t>(timbre),
               chiff_amount_q30, chiff_audible_samples);

  if (!strcmp(mode, "chiff")) {
    // What the caller handed out, then what each destination holds.
    printf("given %u %u\n", chiff_amount_q30, chiff_audible_samples);
    PrintChiff("gain", voice.oscillator_.gain_envelope_);
    PrintChiff("timbre", voice.oscillator_.timbre_envelope_);
    PrintChiff("aux1", aux_1_output.envelope_);
    PrintChiff("aux2", aux_2_output.envelope_);
    return 0;
  }

  if (!strcmp(mode, "dac")) {
    for (int block = 0; block < blocks; ++block) {
      aux_1_output.RenderSamples(0, kAux1Channel, 0);
      for (size_t i = 0; i < kAudioBlockSize; ++i) {
        // The buffer is int16_t and the value in it is a Q16 DAC code, so the
        // top half of the range reads back negative. The DAC takes the bits.
        printf("%u%c", static_cast<uint16_t>(g_dac_block[kAux1Channel][i]),
               i + 1 == kAudioBlockSize ? '\n' : ' ');
      }
    }
    return 0;
  }

  fprintf(stderr, "unknown mode: %s\n", mode);
  return 1;
}
