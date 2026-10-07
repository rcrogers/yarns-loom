// The resonator shapes' common signal flow, in floating point, every stage an
// independent parameter:
//
//   filter input = input noise + feedback
//   feedback     = gain_out * d_c * scale * S(offset + gain_in * s / scale)
//   filter       = Chamberlin SVF, damping d, at the note; s = its bandpass
//   output       = tap (bp | lp | feedback) -> drive -> output stage
//
// d_c is d, or a fixed coupling. gain_out, gain_in, offset and the noise
// amount each follow the gain envelope G when their *_env flag is set.
// Usage: flow key=value ... ; writes float32 samples to o=PATH.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static std::map<std::string, std::string> g_args;
static double Arg(const char* key, double fallback) {
  std::map<std::string, std::string>::const_iterator it = g_args.find(key);
  return it == g_args.end() ? fallback : atof(it->second.c_str());
}
static std::string ArgS(const char* key, const char* fallback) {
  std::map<std::string, std::string>::const_iterator it = g_args.find(key);
  return it == g_args.end() ? fallback : it->second;
}

static double Phi(double x) { return x * exp(0.5 - 0.5 * x * x); }
// blend=k: the "blend" shaper's share of phi against tanh.
static double g_blend = 0.5;
// knee=k: the "phik" shaper, x e^((1 - |x|^2k) / 2k): phi at k = 1, peaking at 1
// where x is 1 like it, with a sharper knee as k grows.
static double g_knee = 1;
static double Shape(int shaper, double x) {
  switch (shaper) {
    case 1: return tanh(x);
    case 2: return Phi(x);
    case 3: return x / sqrt(1 + x * x);
    case 4: return 2 / M_PI * atan(M_PI / 2 * x);
    case 5: return x < -1.5 ? -1 : x > 1.5 ? 1 : x - 4.0 / 27 * x * x * x;
    case 6: return sin(x);
    case 7: return (1 - g_blend) * tanh(x) + g_blend * Phi(x);
    case 8: return x * exp((1 - pow(fabs(x), 2 * g_knee)) / (2 * g_knee));
    default: return x;
  }
}
// First-order antiderivative antialiasing of tanh: the mean of tanh over the
// segment from the previous input to this one, via log cosh.
static double LogCosh(double x) { return fabs(x) + log1p(exp(-2 * fabs(x))) - M_LN2; }
static double TanhAntialiased(double x, double x_previous) {
  const double dx = x - x_previous;
  return fabs(dx) < 1e-9 ? tanh(0.5 * (x + x_previous))
                         : (LogCosh(x) - LogCosh(x_previous)) / dx;
}
// none: no feedback path at all.
static int ShaperIndex(const std::string& name) {
  const char* names[] = { "linear", "tanh", "phi", "algebraic", "atan", "cubic", "sine", "blend", "phik" };
  if (name == "none") return -1;
  for (int i = 0; i < 9; ++i) if (name == names[i]) return i;
  fprintf(stderr, "unknown shaper %s\n", name.c_str());
  exit(1);
}

// Linear-segment ADSR on G, times in seconds.
struct Envelope {
  double attack, decay, sustain, peak, release, key_up;
  double Value(double t, double* level_at_key_up) const {
    if (t < key_up) {
      double g;
      if (t < attack) g = peak * t / attack;
      else if (t < attack + decay) g = peak + (sustain - peak) * (t - attack) / decay;
      else g = sustain;
      *level_at_key_up = g;
      return g;
    }
    const double r = (t - key_up) / release;
    return r >= 1 ? 0 : *level_at_key_up * (1 - r);
  }
};

// A parameter as a function of the controls: "A" constant; "g:A:B" / "gx:A:B"
// linear / geometric from A at gain 0 to B at gain 1; "u:A:B" / "ux:A:B" the
// same in u, TIMBRE's position 0..1; "ctl" the control file's d; "dref:A:R"
// A * R / d, a coupling fixed at R; "tab:c0,c1,..." a table across TIMBRE.
// A comma list as a table evenly across TIMBRE 0..127; one value is a constant.
static std::vector<double> ParseTable(const std::string& list) {
  std::vector<double> table;
  for (size_t start = 0; start < list.size();) {
    const size_t comma = list.find(',', start);
    table.push_back(atof(list.substr(start, comma - start).c_str()));
    start = comma == std::string::npos ? list.size() : comma + 1;
  }
  if (table.size() == 1) table.push_back(table[0]);
  return table;
}
// The table at TIMBRE's position u, interpolated.
static double TableAt(const std::vector<double>& table, double u) {
  const double x = u * (table.size() - 1);
  const size_t k = std::min(static_cast<size_t>(x), table.size() - 2);
  return table[k] + (x - k) * (table[k + 1] - table[k]);
}
// "tab:c0,c1,..." is a table across TIMBRE (ParseTable, TableAt).
struct Spec {
  enum Kind { CONSTANT, GAIN, GAIN_GEOMETRIC, TIMBRE, TIMBRE_GEOMETRIC, CONTROL_D, FIXED_COUPLING, TABLE };
  Kind kind;
  double a, b;
  std::vector<double> table;
};
static Spec ParseSpec(const std::string& text) {
  Spec s = { Spec::CONSTANT, 0, 0, std::vector<double>() };
  if (text == "ctl") { s.kind = Spec::CONTROL_D; return s; }
  if (text.compare(0, 4, "tab:") == 0) {
    s.kind = Spec::TABLE; s.table = ParseTable(text.substr(4)); return s;
  }
  const size_t colon = text.find(':');
  if (colon == std::string::npos) { s.a = atof(text.c_str()); return s; }
  const std::string kind = text.substr(0, colon);
  const std::string rest = text.substr(colon + 1);
  const size_t second = rest.find(':');
  if (second == std::string::npos) { fprintf(stderr, "bad spec %s\n", text.c_str()); exit(1); }
  s.a = atof(rest.substr(0, second).c_str());
  s.b = atof(rest.substr(second + 1).c_str());
  if (kind == "g") s.kind = Spec::GAIN;
  else if (kind == "gx") s.kind = Spec::GAIN_GEOMETRIC;
  else if (kind == "u") s.kind = Spec::TIMBRE;
  else if (kind == "ux") s.kind = Spec::TIMBRE_GEOMETRIC;
  else if (kind == "dref") s.kind = Spec::FIXED_COUPLING;
  else { fprintf(stderr, "bad spec %s\n", text.c_str()); exit(1); }
  return s;
}
static double Eval(const Spec& s, double g, double u, double d) {
  switch (s.kind) {
    case Spec::GAIN: return s.a + (s.b - s.a) * g;
    case Spec::GAIN_GEOMETRIC: return s.a * pow(s.b / s.a, g);
    case Spec::TIMBRE: return s.a + (s.b - s.a) * u;
    case Spec::TIMBRE_GEOMETRIC: return s.a * pow(s.b / s.a, u);
    case Spec::CONTROL_D: return d;
    case Spec::FIXED_COUPLING: return s.a * s.b / d;
    case Spec::TABLE: return TableAt(s.table, u);
    default: return s.a;
  }
}

// A comma list as a table evenly across TIMBRE 0..127; one value is a constant.
static std::vector<double> TableArg(const char* key) { return ParseTable(ArgS(key, "")); }

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const char* eq = strchr(argv[i], '=');
    if (!eq) { fprintf(stderr, "bad arg %s\n", argv[i]); return 1; }
    g_args[std::string(argv[i], eq - argv[i])] = eq + 1;
  }
  const double fs = Arg("fs", 45000);
  const double midi = Arg("midi", 69);
  const double f0 = 440 * pow(2, (midi - 69) / 12);
  const double c = 2 * sin(M_PI * f0 / fs);
  // modes=N: N resonators at k * f0 (k = 1..N, up to fs / 6, where Chamberlin
  // stays stable), each damped d * k^modedamp, all driven by the same input;
  // the shaper reads their summed band-passes -- a string's velocity at the bow.
  std::vector<double> mode_c, mode_bp, mode_lp, mode_damp_factor;
  for (int k = 2; k <= Arg("modes", 1) && k * f0 < fs / 6; ++k) {
    mode_c.push_back(2 * sin(M_PI * k * f0 / fs));
    mode_damp_factor.push_back(pow(k, Arg("modedamp", 0)));
    mode_bp.push_back(0);
    mode_lp.push_back(0);
  }
  const double d_start = Arg("d", 0.01);
  // A Q sweep: d moves geometrically from d to d_end across the note.
  const double d_end = Arg("d_end", d_start);
  const double coupling = Arg("coupling", 0);  // 0: the filter's own d
  const double duration = Arg("dur", 1.5);
  const Envelope env = { Arg("a", 0.0005), Arg("dc", 0.001), Arg("sus", 1),
                         Arg("pk", Arg("sus", 1)), Arg("r", 0.05),
                         Arg("key_up", duration - 0.3) };
  const double noise = Arg("n", 0);
  const bool noise_env = Arg("n_env", 1) != 0;
  // Input x sqrt(d / 2), output divided by it: WIND's make-up.
  const bool make_up = Arg("makeup", 0) != 0;
  const double make_up_floor = Arg("makeup_floor", 2.0 / 256);
  const int shaper = ShaperIndex(ArgS("shaper", "none"));
  g_blend = Arg("blend", g_blend);
  g_knee = Arg("knee", g_knee);
  const double gain_out = Arg("go", 1), gain_in = Arg("gi", 1), offset = Arg("off", 0);
  const bool go_env = Arg("go_env", 0) != 0, gi_env = Arg("gi_env", 0) != 0,
      off_env = Arg("off_env", 0) != 0;
  const double scale = Arg("scale", 1);
  const std::string tap = ArgS("tap", "bp");
  const std::string out_stage = ArgS("out", "lin");
  const double drive = Arg("drive", 1);
  unsigned noise_state = static_cast<unsigned>(Arg("seed", 1)) * 2654435761u | 1;
  // adaa=1: the tanh shaper antialiased (TanhAntialiased).
  const bool antialiased = Arg("adaa", 0) != 0;
  if (antialiased && shaper != 1) { fprintf(stderr, "adaa needs shaper=tanh\n"); return 1; }

  // ctl=PATH: float32 pairs (d, G) per sample, the firmware's own controls,
  // in place of the envelope and d above. The upper-case parameters are specs
  // of them (ParseSpec), each taking over its stage when given: D the
  // filter's damping, N the input noise amount, L the small-signal loop gain
  // (the input gain becomes L / S'(offset), the output gain 1), OFF the
  // offset, VCA a gain after the output stage, DRIVE the output drive, SCALE
  // the feedback scale, DIRECT a coefficient on the gain fed straight into the
  // filter.
  std::vector<float> controls;
  if (g_args.count("ctl")) {
    FILE* cf = fopen(ArgS("ctl", "").c_str(), "rb");
    if (!cf) { fprintf(stderr, "no ctl file\n"); return 1; }
    float pair[2];
    while (fread(pair, sizeof(float), 2, cf) == 2) {
      controls.push_back(pair[0]);
      controls.push_back(pair[1]);
    }
    fclose(cf);
  }
  const bool has_controls = !controls.empty();
  const Spec spec_d = ParseSpec(ArgS("D", has_controls ? "ctl" : "0"));
  const bool has_spec_d = g_args.count("D") || has_controls;
  const bool has_n = g_args.count("N"), has_l = g_args.count("L"),
      has_off = g_args.count("OFF"), has_vca = g_args.count("VCA"),
      has_drive = g_args.count("DRIVE"), has_scale = g_args.count("SCALE"),
      has_direct = g_args.count("DIRECT");
  const Spec spec_n = ParseSpec(ArgS("N", "0")), spec_l = ParseSpec(ArgS("L", "0")),
      spec_off = ParseSpec(ArgS("OFF", "0")), spec_vca = ParseSpec(ArgS("VCA", "1")),
      spec_drive = ParseSpec(ArgS("DRIVE", "1")), spec_scale = ParseSpec(ArgS("SCALE", "1")),
      spec_direct = ParseSpec(ArgS("DIRECT", "0"));

  const int length = has_controls ? static_cast<int>(controls.size() / 2)
                                  : static_cast<int>(duration * fs);
  std::vector<float> out(length);
  double bp = 0, lp = 0, dc_x = 0, dc_y = 0, level_at_key_up = 0, x_previous = 0,
      shaped_previous = 0, last_movement = 0, tap_previous = 0;
  // tapdrive=k: the output tap reads the shaper a second time, k times
  // harder than the loop does, on the same filter output.
  const double tap_drive = Arg("tapdrive", 0);
  const double slip_noise = Arg("slipn", 0);
  const bool slip_into_loop_only = ArgS("slipto", "out") == "loop";
  const double x_noise = Arg("xnoise", 0), x_slip = Arg("xslip", 0);
  // PNOISE: the jitter amount as a spec (tracking gain or TIMBRE), in place
  // of the constant pnoise.
  const bool has_pnoise_spec = g_args.count("PNOISE") != 0;
  const Spec spec_pnoise = ParseSpec(ArgS("PNOISE", "0"));
  const double pressure_noise = Arg("pnoise", 0);
  const bool pressure_on_ac = Arg("pnoiseac", 0) != 0;
  const double tilt = Arg("tilt", 0);
  // scrape=k: the jitter's own part of the force, k * (curve less its rest)
  // * noise, added to the output -- friction noise straight to the bridge,
  // gated by the force, not filtered by the resonance. scrapehz=f high-passes
  // it with one pole at f (0: none), or scrapepitch=r at r times the note.
  const double scrape = Arg("scrape", 0);
  const double scrape_corner = Arg("scrapepitch", 0) > 0 ? Arg("scrapepitch", 0) * f0 : Arg("scrapehz", 0);
  const double scrape_pole = scrape_corner > 0 ? exp(-2 * M_PI * scrape_corner / fs) : 0;
  double scrape_x = 0, scrape_y = 0, scrape_part = 0;
  // Per-CYCLE irregularity: a value drawn uniform in -1..1 at each upward zero
  // crossing of bp, held for that period. cyclecents=c tunes the resonator by
  // that times c cents (the period's length varies); cycleoff=b moves the
  // curve's offset by that times b (its shape varies); cyclepress=k scales the
  // feedback by 1 + that times k (its level varies).
  const double cycle_cents = Arg("cyclecents", 0), cycle_offset = Arg("cycleoff", 0),
      cycle_pressure = Arg("cyclepress", 0);
  const bool per_cycle = cycle_cents || cycle_offset || cycle_pressure;
  double cycle_value = 0, cycle_bp_previous = 0;
  unsigned cycle_state = 0x9e3779b9u;
  // hpmix=m: the filter's high-pass, m times, mixed into the output.
  const double hp_mix = Arg("hpmix", 0);
  const bool rescale = Arg("rescale", 0) != 0;
  double scale_previous = 0;
  double tilt_previous = 0;
  // RETUNE=c0,c1,...: cents to tune the resonator by, evenly across TIMBRE
  // 0..127 and interpolated -- a per-block pitch correction's table.
  const std::vector<double> retune_cents = TableArg("RETUNE");
  // DIRECTTAB=c0,c1,...: the direct term's coefficient as a table across
  // TIMBRE, in place of DIRECT -- a strike sized to the loop's settled level.
  const std::vector<double> direct_table = TableArg("DIRECTTAB");
  // pnoisehz: the jitter's low-pass corner; 0 leaves it white.
  const double pressure_jitter_hz = Arg("pnoisehz", 500);
  const double pressure_jitter_pole =
      pressure_jitter_hz > 0 ? 1 - exp(-2 * M_PI * pressure_jitter_hz / fs) : 1;
  double pressure_jitter = 0;
  for (int i = 0; i < length; ++i) {
    const double t = i / fs;
    const double g = has_controls ? controls[2 * i + 1] : env.Value(t, &level_at_key_up);
    const double d_control = has_controls ? controls[2 * i]
        : d_start * pow(d_end / d_start, t / duration);
    if (per_cycle) {
      if (cycle_bp_previous < 0 && bp >= 0) {
        cycle_state ^= cycle_state << 13;
        cycle_state ^= cycle_state >> 17;
        cycle_state ^= cycle_state << 5;
        cycle_value = static_cast<int>(cycle_state) / 2147483648.0;
      }
      cycle_bp_previous = bp;
    }
    // TIMBRE's position, from the warp d = 2 * 2^(-TI / 8.43), TI 0..127.
    const double u = std::max(0.0, std::min(1.0, -8.43 * log2(d_control / 2) / 127));
    const double d = has_spec_d ? Eval(spec_d, g, u, d_control) : d_control;
    const double d_c = coupling > 0 ? coupling : d;
    noise_state ^= noise_state << 13;
    noise_state ^= noise_state >> 17;
    noise_state ^= noise_state << 5;
    const double white = static_cast<int>(noise_state) / 2147483648.0;
    const double make_up_gain =
        make_up ? sqrt(std::max(std::min(d, 2.0), make_up_floor) / 2) : 1;
    const double in_noise = (has_n ? Eval(spec_n, g, u, d) : noise * (noise_env ? g : 1))
        * make_up_gain * white;
    double feedback = 0, feedback_tap = 0, shaper_drive = 0;
    if (shaper >= 0) {
      const double off = (has_off ? Eval(spec_off, g, u, d) : offset * (off_env ? g : 1))
          + cycle_offset * cycle_value;
      double in_gain = gain_in * (gi_env ? g : 1), out_gain = gain_out * (go_env ? g : 1);
      if (has_l) {
        const double h = 1e-5;
        const double slope = (Shape(shaper, off + h) - Shape(shaper, off - h)) / (2 * h);
        if (fabs(slope) < 1e-3) { fprintf(stderr, "shaper flat at offset %g\n", off); return 1; }
        in_gain = Eval(spec_l, g, u, d) / slope;
        out_gain = 1;
      }
      // SCALE: the feedback scale as a spec, floored so a scale of zero
      // leaves the filter to ring down rather than dividing by it.
      const double scale_now = has_scale ? std::max(Eval(spec_scale, g, u, d), 1e-6) : scale;
      // rescale=1: when the scale falls, the filter state falls with it, so the
      // shaper's input stays where the loop put it instead of growing as the
      // input gain divides by a smaller scale.
      if (rescale && scale_previous > 0 && scale_now < scale_previous) {
        bp *= scale_now / scale_previous;
        lp *= scale_now / scale_previous;
      }
      scale_previous = scale_now;
      double bp_sum = bp;
      for (size_t m = 0; m < mode_bp.size(); ++m) bp_sum += mode_bp[m];
      double x = off + in_gain * bp_sum / scale_now;
      shaper_drive = x - off;
      // XNOISE=k / XSLIP=k: noise on the shaper's INPUT, plain or times how
      // fast its output moved last sample: jitter in when the slip comes.
      if (x_noise || x_slip) {
        noise_state ^= noise_state << 13;
        noise_state ^= noise_state >> 17;
        noise_state ^= noise_state << 5;
        x += (static_cast<int>(noise_state) / 2147483648.0)
            * (x_noise + x_slip * last_movement);
      }
      const double shaped = antialiased ? TanhAntialiased(x, x_previous) : Shape(shaper, x);
      x_previous = x;
      // SLIPN=k: noise on the shaper's output, k times how fast that output
      // moves, so it bursts where the waveform jumps -- once a period.
      // slipto=loop keeps it out of the output tap.
      double y = shaped;
      if (slip_noise) {
        noise_state ^= noise_state << 13;
        noise_state ^= noise_state >> 17;
        noise_state ^= noise_state << 5;
        y += slip_noise * (static_cast<int>(noise_state) / 2147483648.0)
            * fabs(shaped - shaped_previous);
      }
      last_movement = fabs(shaped - shaped_previous);
      shaped_previous = shaped;
      // PNOISE=k: the feedback times 1 + k * noise low-passed at 500 Hz --
      // jitter in how hard the loop drives itself, as bow pressure wavers.
      double pressure = 1;
      if (pressure_noise || has_pnoise_spec || scrape) {
        noise_state ^= noise_state << 13;
        noise_state ^= noise_state >> 17;
        noise_state ^= noise_state << 5;
        pressure_jitter += pressure_jitter_pole
            * (static_cast<int>(noise_state) / 2147483648.0 - pressure_jitter);
        const double amount = has_pnoise_spec ? Eval(spec_pnoise, g, u, d) : pressure_noise;
        pressure = 1 + amount * pressure_jitter;
        if (scrape) scrape_part = scrape * scale_now * (y - Shape(shaper, off)) * pressure_jitter;
      }
      // pnoiseac=1: the jitter on the feedback less its resting value only, as
      // the firmware's loop feeds back the curve less its value at rest.
      if (cycle_pressure) pressure *= 1 + cycle_pressure * cycle_value;
      feedback = pressure_on_ac
          ? out_gain * d_c * scale_now
              * (Shape(shaper, off) + (y - Shape(shaper, off)) * pressure)
          : out_gain * d_c * scale_now * y * pressure;
      feedback_tap = scale_now * ((slip_into_loop_only ? shaped : y) - Shape(shaper, off));
      if (tap_drive) {
        feedback_tap = scale_now
            * (Shape(shaper, off + tap_drive * in_gain * bp_sum / scale_now) - Shape(shaper, off));
      }
    }
    // DIRECT: the gain itself into the filter, as PING takes it -- its steps
    // and the exciter riding on it reach the filter as signal.
    const double direct = !direct_table.empty() ? TableAt(direct_table, u) * g
        : has_direct ? Eval(spec_direct, g, u, d) * g : 0;
    const double notch = in_noise + direct + feedback - std::min(d, 2.0) * bp;
    // RETUNE: the resonator tuned this many cents off the note, read from the
    // table at TIMBRE's position.
    double c_now = c;
    if (!retune_cents.empty() || cycle_cents) {
      const double cents = (retune_cents.empty() ? 0 : TableAt(retune_cents, u)) + cycle_cents * cycle_value;
      c_now = 2 * sin(M_PI * f0 * pow(2, cents / 1200) / fs);
    }
    lp += c_now * bp;
    const double hp = notch - lp;
    bp += c_now * (notch - lp);
    for (size_t m = 0; m < mode_bp.size(); ++m) {
      const double mode_notch = in_noise + direct + feedback
          - std::min(d * mode_damp_factor[m], 2.0) * mode_bp[m];
      mode_lp[m] += mode_c[m] * mode_bp[m];
      mode_bp[m] += mode_c[m] * (mode_notch - mode_lp[m]);
    }
    double o;
    if (tap == "fb" || tap == "dfb") {
      // One-pole DC blocker, 20 Hz.
      o = feedback_tap - dc_x + exp(-2 * M_PI * 20 / fs) * dc_y;
      dc_x = feedback_tap;
      dc_y = o;
      // dfb: its first difference, tilted up 6 dB per octave.
      if (tap == "dfb") {
        const double difference = o - tap_previous;
        tap_previous = o;
        o = difference;
      }
    } else if (tap == "x") {
      // The shaper's input less its offset: how far the loop drives the curve.
      o = shaper_drive;
    } else if (tap == "hp") {
      // The filter's high-pass: the feedback less the mode's own response.
      o = hp;
    } else {
      o = tap == "lp" ? lp : bp;
      if (make_up) o /= make_up_gain;
    }
    if (hp_mix) o += hp_mix * hp;
    if (scrape) {
      const double lifted = scrape_corner > 0
          ? scrape_pole * (scrape_y + scrape_part - scrape_x) : scrape_part;
      scrape_x = scrape_part;
      scrape_y = lifted;
      o += lifted;
    }
    // tilt=b: the output plus b times its first difference -- a treble lift
    // that leaves the fundamental where it is.
    if (tilt) {
      const double lifted = o + tilt * (o - tilt_previous);
      tilt_previous = o;
      o = lifted;
    }
    o *= has_drive ? Eval(spec_drive, g, u, d) : drive;
    o = out_stage == "tanh" ? tanh(o) : std::max(-1.0, std::min(1.0, o));
    if (has_vca) o *= Eval(spec_vca, g, u, d);
    out[i] = static_cast<float>(o);
  }
  FILE* f = fopen(ArgS("o", "/dev/stdout").c_str(), "wb");
  fwrite(&out[0], sizeof(float), out.size(), f);
  fclose(f);
  return 0;
}
