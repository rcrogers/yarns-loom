// Measures WarpTimbre's sign behaviour per shape. No re-derivation: this calls
// the real Oscillator::WarpTimbre from yarns/oscillator.cc.
#define TEST 1
#define private public
#include "yarns/oscillator.h"
#include <cstdio>
#include <algorithm>
#include <cmath>
using namespace yarns;

static const char* kName[] = {
  "NOISE_NOTCH","NOISE_LP","NOISE_BP","NOISE_HP",
  "CZ_PULSE_LP","CZ_PULSE_PK","CZ_PULSE_BP","CZ_PULSE_HP",
  "CZ_SAW_LP","CZ_SAW_PK","CZ_SAW_BP","CZ_SAW_HP",
  "LP_PULSE","LP_SAW","VARIABLE_PULSE","VARIABLE_SAW","SAW_PULSE_MORPH",
  "SYNC_SINE","SYNC_PULSE","SYNC_SAW","DIRAC_COMB","TANH_SINE","EXP_SINE",
  "SINE_THRU_SINE","TRI_THRU_SINE","EXP_THRU_SINE",
  "SINE_THRU_SINE_B","TRI_THRU_SINE_B","EXP_THRU_SINE_B",
  "SINE_THRU_TRI","TRI_THRU_TRI","EXP_THRU_TRI",
  "SINE_THRU_TRI_B","TRI_THRU_TRI_B","EXP_THRU_TRI_B",
  "SINE_THRU_EXP","TRI_THRU_EXP","EXP_THRU_EXP",
  "SINE_THRU_EXP_B","TRI_THRU_EXP_B","EXP_THRU_EXP_B","FM"
};

int main() {
  Oscillator osc;
  osc.Init(32767);
  const int16_t pitch = 60 << 7;   // middle C
  printf("%-18s %8s %8s %8s %8s   %s\n",
         "shape","warp(-32768)","warp(-16384)","warp(0)","warp(+16383)","verdict");
  for (int s = 0; s <= OSC_SHAPE_FM; ++s) {
    osc.set_shape(static_cast<OscillatorShape>(s));
    int32_t wn2 = osc.WarpTimbre(-32768, static_cast<OscillatorShape>(s), pitch);
    int32_t wn1 = osc.WarpTimbre(-16384, static_cast<OscillatorShape>(s), pitch);
    int32_t w0  = osc.WarpTimbre(0,      static_cast<OscillatorShape>(s), pitch);
    int32_t wp  = osc.WarpTimbre(16383,  static_cast<OscillatorShape>(s), pitch);
    // Can a NEGATIVE raw timbre produce a NEGATIVE warped target? That is the
    // only way Envelope::NoteOn(adsr, 0, target) can travel downward.
    bool inverts = (wn2 < 0) || (wn1 < 0);
    // THE PROPOSED FIX: warp the DESTINATION and difference it against the
    // warped bias, instead of warping the modulation amount itself. A warp is
    // an absolute-position map, so warp(delta) is meaningless; warp(bias+delta)
    // - warp(bias) is the delta that map actually implies.
    const int16_t bias = 8192;               // a mid-scale timbre setting
    OscillatorShape sh = static_cast<OscillatorShape>(s);
    int32_t d_neg = osc.WarpTimbreDelta(bias, -16384, sh, pitch);
    int32_t d_pos = osc.WarpTimbreDelta(bias,  16383, sh, pitch);
    printf("%-18s %8d %8d %8d %8d   %-9s | fix: d(-)=%7d d(+)=%7d %s\n",
           kName[s], wn2, wn1, w0, wp, inverts ? "inverts" : "SIGN LOST",
           d_neg, d_pos, (d_neg < 0 && d_pos > 0) ? "MONOTONE" : "still broken");
  }
  // ---- set_shape's rescale ---------------------------------------------
  // On a shape change, Oscillator::set_shape rescales the timbre envelope by
  // WarpTimbre(midpoint, new) / WarpTimbre(midpoint, old) -- a ratio of two
  // ABSOLUTE warped positions. The envelope's target is a signed DELTA, so the
  // ratio that would be exact is WarpTimbreDelta(new) / WarpTimbreDelta(old).
  // Measure how far apart those are, and compare against the ratio that would
  // have been exact BEFORE the delta fix, when the target was WarpTimbre(mod).
  printf("\n== set_shape rescale error, applied vs exact ==\n");
  printf("%-18s -> %-18s %10s %10s %10s\n",
         "from", "to", "applied", "exact(now)", "exact(old)");
  const int16_t kMid = 1 << 14;
  const int pairs[][2] = {
    {OSC_SHAPE_NOISE_LP,      OSC_SHAPE_LP_SAW},
    {OSC_SHAPE_LP_SAW,        OSC_SHAPE_SYNC_SAW},
    {OSC_SHAPE_CZ_SAW_LP,     OSC_SHAPE_NOISE_BP},
    {OSC_SHAPE_VARIABLE_SAW,  OSC_SHAPE_LP_PULSE},
    {OSC_SHAPE_SYNC_SINE,     OSC_SHAPE_CZ_PULSE_LP},
    {OSC_SHAPE_TANH_SINE,     OSC_SHAPE_DIRAC_COMB},
  };
  double worst_now = 0, worst_old = 0;
  for (size_t k = 0; k < sizeof(pairs)/sizeof(pairs[0]); ++k) {
    OscillatorShape a = static_cast<OscillatorShape>(pairs[k][0]);
    OscillatorShape b = static_cast<OscillatorShape>(pairs[k][1]);
    double applied = double(osc.WarpTimbre(kMid, b, pitch))
                   / double(osc.WarpTimbre(kMid, a, pitch));
    // averaged over a spread of bias/mod, since the exact ratio depends on both
    double sn = 0, so = 0; int n = 0;
    for (int bias = 4096; bias <= 28672; bias += 8192)
      for (int mod = -12288; mod <= 12288; mod += 6144) {
        if (!mod) continue;
        double da = osc.WarpTimbreDelta(bias, mod, a, pitch);
        double db = osc.WarpTimbreDelta(bias, mod, b, pitch);
        if (da != 0) { sn += db / da; }
        double oa = osc.WarpTimbre(mod, a, pitch);
        double ob = osc.WarpTimbre(mod, b, pitch);
        if (oa != 0) { so += ob / oa; }
        ++n;
      }
    double now = sn / n, old = so / n;
    printf("%-18s -> %-18s %10.3f %10.3f %10.3f\n",
           kName[pairs[k][0]], kName[pairs[k][1]], applied, now, old);
    double en = applied ? fabs(now - applied) / fabs(applied) : 0;
    double eo = applied ? fabs(old - applied) / fabs(applied) : 0;
    if (en > worst_now) worst_now = en;
    if (eo > worst_old) worst_old = eo;
  }
  printf("\nworst relative error vs the applied ratio:"
         "  delta target %.0f%%   pre-fix target %.0f%%\n",
         100*worst_now, 100*worst_old);
  return 0;
}
