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
#include "tools/panel_chain.h"
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
  adsr.attack_u32 = PanelStageIncrement(attack_setting, 0, 0);
  adsr.decay_u32 = PanelStageIncrement(decay_setting, 0, 0);
  adsr.release_u32 = PanelStageIncrement(release_setting, 0, 0);

  // The two the CV path is here to carry. Part::VoiceNoteOn forms the amount as
  // a fraction of full scale and the duration as an absolute sample count off
  // its own table -- both wider than a byte, which is the whole point.
  const uint32_t chiff_amount_q30 = PanelChiffAmount_q30(amount, 0, 0);
  const uint32_t chiff_audible_samples =
      PanelChiffAudibleSamples(duration, 0, 0);

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

  // THE AUDIO OUTPUT'S SPAN, IN THE CODES THE DAC ACTUALLY TAKES. Every other
  // harness here measures in the int16 the render writes, which is not the
  // same unit and cannot answer how close a voice sits to the wrap.
  if (!strcmp(mode, "span")) {
    CVOutput out;
    out.Init(true);
    printf("  volts_dac_code(0)=%u  volts_dac_code(5)=%u  volts_dac_code(7)=%u\n",
           out.volts_dac_code(0), out.volts_dac_code(5), out.volts_dac_code(7));
    const uint16_t five_v = out.volts_dac_code(0) - out.volts_dac_code(5);
    printf("  5 V span = %u codes, so %.1f codes/V\n", five_v, five_v / 5.0);
    printf("  voices  coherent  incoherent  peak_code  volts_peak  of_5V_span\n");
    for (uint8_t n = 1; n <= 4; ++n) {
      const uint16_t full = five_v * 2;
      const uint16_t coh = full / n;
      const uint16_t inc = (uint16_t) IntegerSqrt((uint32_t) full * coh);
      // The envelope saturates at kEnvelopeSampleMax whatever peak it is given.
      const uint32_t gain = coh > kEnvelopeSampleMax ? kEnvelopeSampleMax : coh;
      printf("  %5u  %8u  %10u  %9u  %9.2f  %9.1f%%\n", n, coh, inc,
             (unsigned) gain, gain / (five_v / 5.0), 100.0 * gain / five_v);
    }
    // What the render actually writes, through the real path. The model above
    // is arithmetic; this is the wire.
    static Voice voices[4];
    static CVOutput audio;
    const uint16_t zero = out.volts_dac_code(0);
    const int only_shape = OptInt(argc, argv, "shape", -1);
    for (int exciter = 0; exciter <= 127; exciter += 127) {
      printf("\n  shape %d, EXCITER %d:\n",
             only_shape < 0 ? (int) OSC_SHAPE_VARIABLE_SAW : only_shape, exciter);
      printf("    voices   low      high     volts_pp   of_10Vpp   rms_codes  outside\n");
      for (uint8_t n = 1; n <= 4; ++n) {
        for (uint8_t i = 0; i < 4; ++i) voices[i].Init();
        audio.Init(true);
        for (uint8_t i = 0; i < n; ++i) {
          voices[i].set_oscillator_mode(OSCILLATOR_MODE_ENVELOPED);
        }
        audio.AssignVoices(&voices[0], DC_PITCH, n, n);
        ADSR a = {0};
        a.peak_u16 = 65535; a.sustain_u16 = 65535;
        a.attack_u32 = 1u << 26; a.decay_u32 = 1u << 26; a.release_u32 = 1u << 26;
        const uint32_t amt = PanelChiffAmount_q30(exciter, 0, 0);
        const uint32_t dur = PanelChiffAudibleSamples(64, 0, 0);
        for (uint8_t i = 0; i < n; ++i) {
          voices[i].oscillator()->set_shape(static_cast<OscillatorShape>(
              only_shape < 0 ? OSC_SHAPE_VARIABLE_SAW : only_shape));
          voices[i].NoteOn(60 << 7, 127, 0, 0, true, a,
                           kEnvelopeSampleMax, amt, dur);
        }
        int32_t lo = INT32_MAX, hi = INT32_MIN;
        double sumsq = 0; long count = 0;
        bool outside = false;
        for (int b = 0; b < 600; ++b) {
          for (uint8_t i = 0; i < n; ++i) voices[i].Refresh();
          audio.RenderSamples(0, 0, 0);
          for (size_t k = 0; k < kAudioBlockSize; ++k) {
            const uint16_t code = static_cast<uint16_t>(g_dac_block[0][k]);
            const int32_t e = static_cast<int32_t>(code) - zero;
            if (e < lo) lo = e;
            if (e > hi) hi = e;
            sumsq += (double) e * e; ++count;
            if (code > 64852 || code < 13522) outside = true;
          }
        }
        const double rms = sqrt(sumsq / count);
        printf("    %5u  %+7d  %+7d  %9.2f  %8.1f%%  %8.0f  %s\n", n, lo, hi,
               (hi - lo) / 5133.0, 100.0 * (hi - lo) / (2 * five_v), rms,
               outside ? "YES" : "no");
      }
    }
    return 0;
  }

  // NO SHAPE MAY LEAVE THE CALIBRATED RANGE, at any voice count. The span is
  // 10 Vpp with 683 codes above it, and past those the code WRAPS and the
  // output jumps to the opposite rail -- so this is not a clip that sounds bad,
  // it is a discontinuity. `span` prints how close the worst shape sits.
  if (!strcmp(mode, "headroom")) {
    static Voice voices[4];
    static CVOutput audio;
    CVOutput probe; probe.Init(true);
    const uint16_t zero = probe.volts_dac_code(0);
    const uint16_t five_v = zero - probe.volts_dac_code(5);
    int failures = 0;
    int32_t worst = 0; int worst_shape = -1, worst_n = 0;
    for (int shape = 0; shape <= OSC_SHAPE_FM; ++shape) {
      for (uint8_t n = 1; n <= 4; ++n) {
        for (uint8_t i = 0; i < 4; ++i) voices[i].Init();
        audio.Init(true);
        for (uint8_t i = 0; i < n; ++i) {
          voices[i].set_oscillator_mode(OSCILLATOR_MODE_ENVELOPED);
        }
        audio.AssignVoices(&voices[0], DC_PITCH, n, n);
        ADSR a = {0};
        a.peak_u16 = 65535; a.sustain_u16 = 65535;
        a.attack_u32 = 1u << 26; a.decay_u32 = 1u << 26; a.release_u32 = 1u << 26;
        const uint32_t amt = PanelChiffAmount_q30(127, 0, 0);
        const uint32_t dur = PanelChiffAudibleSamples(64, 0, 0);
        for (uint8_t i = 0; i < n; ++i) {
          voices[i].oscillator()->set_shape(static_cast<OscillatorShape>(shape));
          voices[i].NoteOn((48 + 12 * i) << 7, 127, 0, 0, true, a,
                           kEnvelopeSampleMax, amt, dur);
        }
        int32_t peak = 0; bool outside = false;
        for (int b = 0; b < 180; ++b) {
          for (uint8_t i = 0; i < n; ++i) voices[i].Refresh();
          audio.RenderSamples(0, 0, 0);
          for (size_t k = 0; k < kAudioBlockSize; ++k) {
            const uint16_t code = static_cast<uint16_t>(g_dac_block[0][k]);
            const int32_t e = static_cast<int32_t>(code) - zero;
            const int32_t m = e < 0 ? -e : e;
            if (m > peak) peak = m;
            if (code > 64852 || code < 13522) outside = true;
          }
        }
        if (peak > worst) { worst = peak; worst_shape = shape; worst_n = n; }
        if (outside) {
          printf("FAIL shape %d at %u voices left the calibrated range\n", shape, n);
          ++failures;
        }
      }
    }
    printf("%s %d shapes x 4 voice counts stay inside the 10 Vpp span"
           "  [worst %d of %u codes, %d left, shape %d at %d voice%s]\n",
           failures ? "FAIL" : "PASS", OSC_SHAPE_FM + 1, worst, five_v,
           five_v - worst, worst_shape, worst_n, worst_n == 1 ? "" : "s");
    return failures ? 1 : 0;
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

  // THE PITCH PIPELINE'S RESOLUTION. Every term that moves the pitch computes
  // more precision than a whole pitch unit, and each used to shift it away. A CZ
  // shape's aliases move up to 43 times faster than the note, so a whole unit of
  // pitch is a 41 Hz step in an audible tone -- the artifact was a staircase.
  //
  // The property is BEHAVIOURAL, not a copy of the arithmetic: move one control
  // by its own smallest step and the oscillator's phase increment has to move
  // with it. A plateau means a fraction is being dropped.
  //
  // DEMONSTRATED against the commits before each fix: bend 15 of 511 steps,
  // portamento 128 of 1821 refreshes -- 128 being exactly the pitch units in
  // the semitone it glides -- and the slow vibrato sitting still for 496 ms of
  // every 5000.
  if (!strcmp(mode, "pitch")) {
    int failures = 0;

    // PITCH BEND, at its default range of two semitones. 16384 bend steps cover
    // 256 pitch units, so 64 bend steps share a unit and 63 of every 64 landed
    // on a plateau.
    voice.set_pitch_bend_range(2);
    uint32_t previous = 0;
    int moved = 0;
    const int kBendSteps = 512;
    for (int i = 0; i < kBendSteps; ++i) {
      voice.PitchBend(static_cast<uint16_t>(8192 + i));
      voice.Refresh();
      if (i && voice.oscillator_.phase_increment_ != previous) ++moved;
      previous = voice.oscillator_.phase_increment_;
    }
    printf("%s bend: %d of %d steps moved the increment\n",
           moved > kBendSteps * 9 / 10 ? "PASS" : "FAIL", moved, kBendSteps - 1);
    if (moved <= kBendSteps * 9 / 10) ++failures;
    voice.PitchBend(8192);

    // PORTAMENTO, over ONE SEMITONE. A wide glide crosses a whole pitch unit
    // every refresh whatever the resolution, so it cannot see this: the
    // interval has to be narrow enough that the glide spends several refreshes
    // inside one unit.
    voice.NoteOn(60 << 7, static_cast<uint8_t>(velocity), 0, 0, true, adsr,
                 static_cast<int16_t>(timbre), chiff_amount_q30,
                 chiff_audible_samples);
    voice.NoteOn(61 << 7, static_cast<uint8_t>(velocity), 32, 0, true, adsr,
                 static_cast<int16_t>(timbre), chiff_amount_q30,
                 chiff_audible_samples);
    previous = 0;
    moved = 0;
    int refreshes = 0;
    while (voice.portamento_phase_increment_ && refreshes < 20000) {
      voice.Refresh();
      if (refreshes && voice.oscillator_.phase_increment_ != previous) ++moved;
      previous = voice.oscillator_.phase_increment_;
      ++refreshes;
    }
    printf("%s portamento: %d of %d refreshes moved the increment\n",
           moved > refreshes * 3 / 4 ? "PASS" : "FAIL", moved, refreshes - 1);
    if (moved <= refreshes * 3 / 4) ++failures;

    // A SLOW VIBRATO, which is the case a fast one HIDES. The pitch LFO's
    // interpolator reaches its 125 Hz target in 8 ms and then holds, so if that
    // target is quantised the note sits still for the rest of the sample --
    // half a second at a 5 s period -- and then jumps. A CZ fold moves up to 43
    // times faster than the note, which turns each of those steps into a 40 Hz
    // leap in an audible tone. Measured as DWELL, because level and swing are
    // both unchanged by it: the artifact steps at constant loudness.
    const int kVibratoPeriodMs = 5000;
    voice.set_vibrato_range(1);
    voice.set_vibrato_mod(10);
    voice.lfo(LFO_ROLE_PITCH)->SetPhaseIncrement(static_cast<uint32_t>(
        4294967296.0 * 1000.0 / (kVibratoPeriodMs * (double) kRefreshHz)));
    voice.NoteOn(96 << 7, static_cast<uint8_t>(velocity), 0, 0, true, adsr,
                 static_cast<int16_t>(timbre), chiff_amount_q30,
                 chiff_audible_samples);
    for (int i = 0; i < kRefreshHz / 2; ++i) voice.Refresh();
    previous = 0;
    int run = 0, longest_run = 0;
    const int vibrato_ticks = 2 * kRefreshHz * kVibratoPeriodMs / 1000;
    for (int i = 0; i < vibrato_ticks; ++i) {
      voice.Refresh();
      const uint32_t increment = voice.oscillator_.phase_increment_;
      if (increment == previous) { if (++run > longest_run) longest_run = run; }
      else { run = 0; previous = increment; }
    }
    const int dwell_ms = 1000 * longest_run / kRefreshHz;
    // Two 125 Hz LFO samples. The shallowest vibrato at the slowest rate holds
    // that long at the sine's turning point, where the note is stationary
    // anyway; a quantised target holds for 496.
    const int kDwellBudgetMs = 50;
    printf("%s slow vibrato: the pitch sits still for at most %d ms of a %d ms "
           "sweep\n", dwell_ms <= kDwellBudgetMs ? "PASS" : "FAIL", dwell_ms,
           kVibratoPeriodMs);
    if (dwell_ms > kDwellBudgetMs) ++failures;
    voice.set_vibrato_mod(0);

    // AND THE NOTE MUST NOT DRIFT. With nothing modulating it, the fraction has
    // to settle to zero: a standing one detunes a held note by up to a unit.
    voice.NoteOn(60 << 7, static_cast<uint8_t>(velocity), 0, 0, true, adsr,
                 static_cast<int16_t>(timbre), chiff_amount_q30,
                 chiff_audible_samples);
    for (int i = 0; i < 4000; ++i) voice.Refresh();
    const uint32_t settled = voice.oscillator_.phase_increment_;
    voice.oscillator_.Refresh(60 << 7, 0, 0, 0);
    printf("%s held note: increment %u against %u for the bare pitch\n",
           settled == voice.oscillator_.phase_increment_ ? "PASS" : "FAIL",
           settled, voice.oscillator_.phase_increment_);
    if (settled != voice.oscillator_.phase_increment_) ++failures;

    return failures ? 1 : 0;
  }

  fprintf(stderr, "unknown mode: %s\n", mode);
  return 1;
}
