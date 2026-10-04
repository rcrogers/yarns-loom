// WHAT A PART SOUNDS LIKE, not what one oscillator does. osctest renders a
// single shape's function; this renders N voices of a part SUMMING INTO ONE
// MIX, which is the only place a difference between coherent and incoherent
// addition can show.
//
// It exists for a question osctest cannot reach: a shape switch reaches every
// voice in the same sample, so N identical transients add to N times one, while
// the steady state of an uncorrelated shape adds to sqrt(N). The gain budget in
// voice.h is built on the second -- `coherent_scale_codes_u16 = scale /
// num_audio_voices_`, "uncorrelated ... reaching only sqrt(n) times one" -- and
// a switch briefly makes the first true instead.
//
//   paratest voices=4 pitch=84 unison=1 from=3 to=4      one summed render
//   paratest compare                                      the table
// WHAT IT DOES NOT MODEL, so a null result here is not a null result on the
// module: no tremolo and no gain bias (Refresh is handed 0), no timbre LFO --
// the bias is whatever the caller passes and does not move -- no portamento,
// and every voice takes its note in the same sample. kScaleCodes is the span
// voice.h derives from the DAC table, copied rather than computed, so it drifts
// if that table does.
#include "yarns/oscillator.h"
#include "yarns/envelope.h"
#include "yarns/resources.h"
#include "yarns/utils.h"
#include "stmlib/utils/random.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>

using namespace yarns;

static const double kAudioRate = 45000.0;
// Half the 10 Vpp span, as voice.h derives it: (volts_dac_code(0) -
// volts_dac_code(5)) * 2 >> 1. Hard-coded so this tool needs no DAC table.
static const uint16_t kScaleCodes = 25665;

static int OptInt(int argc, char** argv, const char* key, int fallback) {
  const size_t n = strlen(key);
  for (int i = 1; i < argc; ++i)
    if (!strncmp(argv[i], key, n) && argv[i][n] == '=') return atoi(argv[i] + n + 1);
  return fallback;
}

static uint32_t EnvIncrement(int setting) {
  return Interpolate88(lut_envelope_phase_increments, setting << 8);
}

// A part's worth of audio voices, sharing one mix buffer.
static const int kMaxVoices = 4;

struct Part {
  // Fixed, as the firmware holds them: Oscillator is not copyable.
  Oscillator voices[kMaxVoices];
  int n;
  uint16_t coherent, incoherent_scale_ratio_u2_14;

  // alloc is what the PART divides its span by -- every voice it can sound --
  // and n is how many are actually playing. A part holds its allotment whether
  // or not the player is using it, and the gain step a switch makes is
  // kEnvelopeSampleMax over that allotment.
  void Init(int voice_count, int alloc) {
    wrapped = 0; railed = 0;
    n = voice_count;
    coherent = kScaleCodes / alloc;
    incoherent_scale_ratio_u2_14 = static_cast<uint16_t>(
        IntegerSqrt(static_cast<uint32_t>(alloc) << (2 * 14)));
    // Each voice draws its own noise stream, as Oscillator::Init does per voice.
    for (int i = 0; i < n; ++i) voices[i].Init(coherent, incoherent_scale_ratio_u2_14);
  }
  void NoteOff() {
    for (int i = 0; i < n; ++i) voices[i].NoteOff();
  }
  void SetShape(OscillatorShape s) {
    for (int i = 0; i < n; ++i) voices[i].set_shape(s);
  }
  // The envelope keeps a POINTER to its ADSR, as Voice holds its own adsr_, so
  // these outlive NoteOn.
  ADSR adsr[kMaxVoices];
  uint32_t attack_inc = 0, decay_inc = 0;   // 0 = take the setting's LUT entry
  int release = -1;   // RELEASE setting; -1 = the decay setting's, as before
  uint16_t peak = 65535;
  void NoteOn(const int* pitches, int16_t timbre, int attack, int decay,
              uint16_t sustain, uint32_t chiff, uint32_t chiff_samples) {
    // timbre here is raw_max_timbre: TIMBRE ENV MOD, a signed offset from the
    // knob, which Refresh supplies separately as the bias.
    // chiff is EXCITER AMOUNT as part.cc forms it, and part.cc clamps there.
    if (chiff > (1u << 30)) chiff = 1u << 30;
    for (int i = 0; i < n; ++i) {
      ADSR& a = adsr[i];
      a.peak_u16 = peak; a.sustain_u16 = sustain;
      a.attack_u32 = attack_inc ? attack_inc : EnvIncrement(attack);
      a.decay_u32 = decay_inc ? decay_inc : EnvIncrement(decay);
      a.release_u32 = release >= 0 ? EnvIncrement(release)
          : decay_inc ? decay_inc : EnvIncrement(decay);
      voices[i].NoteOn(a, false, pitches[i] << 7, pitches[i] << 7, timbre,
                       chiff, chiff_samples);
    }
  }
  // Voices summed one at a time so the sum can also be formed WIDE. The
  // firmware's is `static_cast<int16_t>(*audio_mix + mixed)`, which wraps, and
  // a wrap in the sum is a full-scale sample this tool would otherwise report
  // as signal. wrapped counts them; a run that reports any is not measuring
  // what it claims to.
  long wrapped, railed;
  // solo >= 0: every voice renders, only that one reaches the mix.
  int solo = -1;
  void RenderBlock(const int* pitches, int16_t timbre, int16_t* mix) {
    // AS THE FIRMWARE FILLS IT. CVOutput::Render does not zero the block: it
    // fills it with zero_dac_code_ = volts_dac_code(0) = 39187 and the voices
    // add into that. The accumulator is an int16 holding a uint16 CODE, so the
    // sum is modular and an excursion that leaves the calibrated span does not
    // clip -- the code wraps and the output jumps to the opposite rail. Starting
    // from 0 instead hides that entirely.
    static const int kZeroDacCode = 39187;
    static const int kSpanCodes = 25665;          // +/-5 V about the zero code
    int32_t wide[kAudioBlockSize];
    for (size_t i = 0; i < kAudioBlockSize; ++i) {
      mix[i] = static_cast<int16_t>(kZeroDacCode);
      wide[i] = kZeroDacCode;
    }
    int16_t one[kAudioBlockSize];
    for (int v = 0; v < n; ++v) {
      for (size_t i = 0; i < kAudioBlockSize; ++i) one[i] = 0;
      voices[v].Refresh(pitches[v] << 7, timbre, 0);
      voices[v].Render(one);                 // this voice's own contribution
      if (solo >= 0 && solo != v) continue;
      for (size_t i = 0; i < kAudioBlockSize; ++i) {
        wide[i] += one[i];
        mix[i] = static_cast<int16_t>(mix[i] + one[i]);   // as the firmware sums
      }
    }
    for (size_t i = 0; i < kAudioBlockSize; ++i) {
      // What the DAC is actually handed, as an excursion about 0 V. Past the
      // span the real output has jumped to the other rail; count those.
      const int code = static_cast<uint16_t>(mix[i]);
      int excursion = code - kZeroDacCode;
      if (excursion > 32767) excursion -= 65536;
      if (excursion < -32768) excursion += 65536;
      if (excursion > kSpanCodes || excursion < -kSpanCodes) ++railed;
      if (wide[i] - kZeroDacCode != excursion) ++wrapped;
      mix[i] = static_cast<int16_t>(excursion);
    }
  }
};

static void Run(Part* p, const int* pitches, int16_t timbre, int blocks,
                std::vector<int>* out) {
  for (int b = 0; b < blocks; ++b) {
    int16_t mix[kAudioBlockSize];
    p->RenderBlock(pitches, timbre, mix);
    if (out) for (size_t i = 0; i < kAudioBlockSize; ++i) out->push_back(mix[i]);
  }
}

// Local peak-to-peak/2 over 64-sample windows: a slow envelope does not read
// as signal, and a one-block transient is not averaged away.
static double LocalAc(const std::vector<int>& x) {
  double best = 0;
  for (size_t i = 0; i + 64 <= x.size(); i += 32) {
    int lo = x[i], hi = x[i];
    for (size_t j = i; j < i + 64; ++j) { if (x[j] < lo) lo = x[j]; if (x[j] > hi) hi = x[j]; }
    const double a = (hi - lo) / 2.0;
    if (a > best) best = a;
  }
  return best;
}
static int Peak(const std::vector<int>& x) {
  int p = 0; for (size_t i = 0; i < x.size(); ++i) if (abs(x[i]) > p) p = abs(x[i]);
  return p;
}
static double DecayMs(const std::vector<int>& x) {
  std::vector<double> e;
  for (size_t i = 0; i + 64 <= x.size(); i += 64) {
    int lo = x[i], hi = x[i];
    for (size_t j = i; j < i + 64; ++j) { if (x[j] < lo) lo = x[j]; if (x[j] > hi) hi = x[j]; }
    e.push_back((hi - lo) / 2.0);
  }
  double pk = 0; size_t at = 0;
  for (size_t i = 0; i < e.size(); ++i) if (e[i] > pk) { pk = e[i]; at = i; }
  for (size_t i = at; i < e.size(); ++i) if (e[i] < pk / 10.0)
    return (i - at) * 64.0 / kAudioRate * 1000.0;
  return -1.0;
}

static void Pitches(int* out, int n, int base, bool unison) {
  static const int kChord[] = {0, 4, 7, 12};
  for (int i = 0; i < n; ++i) out[i] = base + (unison ? 0 : kChord[i % 4]);
}

int main(int argc, char** argv) {
  const char* mode = argc > 1 ? argv[1] : "compare";
  const int pitch = OptInt(argc, argv, "pitch", 84);
  const int16_t timbre = OptInt(argc, argv, "timbre", 14000);
  // TIMBRE ENV MOD: where the timbre envelope pulls Q to, against the knob.
  const int16_t te = OptInt(argc, argv, "te", 0);
  const int attack = OptInt(argc, argv, "attack", 0);
  const int decay = OptInt(argc, argv, "decay", 30);
  const int sustain = OptInt(argc, argv, "sustain", 65535);
  // kAudioBlockSize samples each: 64 at 45 kHz is 1.42 ms.
  const int blocks = OptInt(argc, argv, "blocks", 60);
  const int settle = OptInt(argc, argv, "settle", 400);
  // Raw increments, as unsigned: 0xffffffff is a one-sample stage, where the
  // LUT's fastest entry is four.
  const char* ai = NULL; const char* di = NULL;
  for (int i = 1; i < argc; ++i) {
    if (!strncmp(argv[i], "attack_inc=", 11)) ai = argv[i] + 11;
    if (!strncmp(argv[i], "decay_inc=", 10)) di = argv[i] + 10;
  }
  const uint32_t attack_inc = ai ? strtoul(ai, NULL, 0) : 0;
  const uint32_t decay_inc = di ? strtoul(di, NULL, 0) : 0;
  const int from = OptInt(argc, argv, "from", OSC_SHAPE_NOISE_HP);
  const int to = OptInt(argc, argv, "to", OSC_SHAPE_WIND);
  // EXCITER AMOUNT as a fraction of its clamp, and DURATION as its setting.
  // exciter_q30 overrides exciter with the raw amount, for scaling it exactly.
  const uint32_t exciter = OptInt(argc, argv, "exciter_q30", 0)
      ? static_cast<uint32_t>(OptInt(argc, argv, "exciter_q30", 0))
      : static_cast<uint32_t>(OptInt(argc, argv, "exciter", 0)) * ((1u << 30) / 127);
  const uint32_t exciter_samples = exciter ? ChiffAudibleSamples(Interpolate88(
      lut_chiff_phase_increments, OptInt(argc, argv, "exciter_dur", 40) << 8)) : 0;

  if (!strcmp(mode, "dump")) {
    const int n = OptInt(argc, argv, "voices", 4);
    const bool unison = OptInt(argc, argv, "unison", 1) != 0;
    const bool switching = OptInt(argc, argv, "switch", 1) != 0;
    int pit[8]; Pitches(pit, n, pitch, unison);
    Part p; p.Init(n, OptInt(argc, argv, "alloc", n));
    p.solo = OptInt(argc, argv, "solo", -1);
    p.peak = OptInt(argc, argv, "peak", 65535);
    p.release = OptInt(argc, argv, "release", -1);
    p.attack_inc = attack_inc; p.decay_inc = decay_inc;
    // prime: a shape selected BEFORE `from`, so per-shape members a render
    // leaves behind are set the way a session leaves them. RenderWind's
    // previous_damp_drive_u15_ is the one that matters -- it is 0 until
    // WIND has run once, and until then the state rescale is skipped.
    // prime_note=0 primes without a note: Refresh still settles the timbre
    // bias the note's TIMBRE ENV MOD is warped against, and the envelopes stay
    // at rest, so the note starts from zero.
    const int prime = OptInt(argc, argv, "prime", -1);
    if (prime >= 0) {
      p.SetShape(static_cast<OscillatorShape>(prime));
      if (OptInt(argc, argv, "prime_note", 1))
        p.NoteOn(pit, te, attack, decay, sustain, exciter, exciter_samples);
      Run(&p, pit, timbre, OptInt(argc, argv, "prime_blocks", 200), NULL);
      p.NoteOff();
      Run(&p, pit, timbre, OptInt(argc, argv, "prime_settle", 200), NULL);
    }
    p.SetShape(static_cast<OscillatorShape>(switching ? from : to));
    // noteon=0 leaves the part SILENT: the gain envelope never leaves DEAD.
    // A filtered-noise shape still drives its filter at full scale there --
    // the gain it is missing is spent on its OUTPUT -- so the filter fills
    // while nothing is heard.
    if (OptInt(argc, argv, "noteon", 1))
      p.NoteOn(pit, te, attack, decay, sustain, exciter, exciter_samples);
    // Pre-roll, so the event is heard against what came before it. Without a
    // switch the note-on IS the event, so there is nothing to settle through:
    // settling past it would discard the whole of a percussive note.
    const int pre = OptInt(argc, argv, "pre", 0);
    const int lead = switching ? settle : 0;
    // The noise shapes read timbre as a CUTOFF, resonance coming from pitch;
    // the resonators read pitch as the cutoff. So the frequency the state is
    // charged at and the one it is asked to ring at come from different
    // controls, and a single timbre for both phases mismatches them.
    const int charge_timbre = OptInt(argc, argv, "charge_timbre", timbre);
    Run(&p, pit, charge_timbre, lead > pre ? lead - pre : 0, NULL);
    std::vector<int> v;
    Run(&p, pit, charge_timbre, lead > pre ? pre : lead, &v);
    if (switching) p.SetShape(static_cast<OscillatorShape>(to));
    // release_at: blocks into the capture at which the key is let go, so one
    // file holds the charge AND the ring it leaves behind.
    const int release_at = OptInt(argc, argv, "release_at", 0);
    // timbre_end: the TIMBRE knob ramps linearly from timbre to this across
    // the capture, a block at a time.
    const int timbre_end = OptInt(argc, argv, "timbre_end", timbre);
    for (int b = 0; b < blocks; ++b) {
      if (b == release_at) p.NoteOff();
      const int16_t t = static_cast<int16_t>(
          timbre + (timbre_end - timbre) * b / (blocks > 1 ? blocks - 1 : 1));
      Run(&p, pit, t, 1, &v);
    }
    for (size_t i = 0; i < v.size(); ++i) printf("%d\n", v[i]);
    if (p.railed)
      fprintf(stderr, "NOTE: the mix code left the +/-5 V span on %ld samples -- "
              "on hardware those jump to the opposite rail\n", p.railed);
    if (p.wrapped)
      fprintf(stderr, "WARNING: the voice sum wrapped on %ld samples -- "
              "those are full-scale artifacts, not signal\n", p.wrapped);
    return 0;
  }

  printf("shape switch vs note-on, summed over a part's voices\n");
  printf("pitch %d, timbre %d, ATTACK %d, SUSTAIN %s, from shape %d to %d\n\n",
         pitch, timbre, attack, sustain ? "full" : "0", from, to);
  const int alloc = OptInt(argc, argv, "alloc", 4);
  printf("part allots for %d voices; sweeping how many actually sound\n\n", alloc);
  printf("%-7s %-8s %10s %10s %13s %8s %9s\n",
         "sounding", "spacing", "on AC", "on peak", "sw AC lo..hi", "sw peak", "sw decay");
  for (int n = 1; n <= alloc; ++n) {
    for (int u = 1; u >= 0; --u) {
      if (n == 1 && u == 0) continue;            // one voice has no spacing
      int pit[8]; Pitches(pit, n, pitch, u != 0);
      std::vector<int> a, b;
      { Part p; p.Init(n, alloc);
        p.attack_inc = attack_inc; p.decay_inc = decay_inc;
        p.SetShape(static_cast<OscillatorShape>(to));
        p.NoteOn(pit, te, attack, decay, sustain, exciter, exciter_samples);
        Run(&p, pit, timbre, blocks, &a); }
      double sw_ac = 0, sw_lo = 1e18, sw_pk = 0, sw_dec = 0;
      for (int s = 0; s < 4; ++s) {              // the primed state is random
        std::vector<int> t;
        Part p; p.Init(n, alloc);
        p.attack_inc = attack_inc; p.decay_inc = decay_inc;
        p.SetShape(static_cast<OscillatorShape>(from));
        p.NoteOn(pit, te, attack, decay, sustain, exciter, exciter_samples);
        Run(&p, pit, timbre, settle + s * 11, NULL);
        p.SetShape(static_cast<OscillatorShape>(to));
        Run(&p, pit, timbre, blocks, &t);
        const double ac = LocalAc(t);
        if (ac < sw_lo) sw_lo = ac;
        if (ac > sw_ac) { sw_ac = ac; sw_pk = Peak(t); sw_dec = DecayMs(t); }
      }
      char dec[16];
      if (sw_dec < 0) snprintf(dec, sizeof dec, "--");
      else snprintf(dec, sizeof dec, "%.0f ms", sw_dec);
      printf("%-7d %-8s %10.0f %10d %6.0f..%-6.0f %8.0f %9s\n",
             n, u ? "unison" : "chord", LocalAc(a), Peak(a), sw_lo, sw_ac, sw_pk, dec);
    }
  }
  return 0;
}
