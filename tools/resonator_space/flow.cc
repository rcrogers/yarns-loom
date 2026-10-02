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
static double Shape(int shaper, double x) {
  switch (shaper) {
    case 1: return tanh(x);
    case 2: return Phi(x);
    case 3: return x / sqrt(1 + x * x);
    case 4: return 2 / M_PI * atan(M_PI / 2 * x);
    case 5: return x < -1.5 ? -1 : x > 1.5 ? 1 : x - 4.0 / 27 * x * x * x;
    default: return x;
  }
}
// none: no feedback path at all.
static int ShaperIndex(const std::string& name) {
  const char* names[] = { "linear", "tanh", "phi", "algebraic", "atan", "cubic" };
  if (name == "none") return -1;
  for (int i = 0; i < 6; ++i) if (name == names[i]) return i;
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
// A * R / d, a coupling fixed at R.
struct Spec {
  enum Kind { CONSTANT, GAIN, GAIN_GEOMETRIC, TIMBRE, TIMBRE_GEOMETRIC, CONTROL_D, FIXED_COUPLING };
  Kind kind;
  double a, b;
};
static Spec ParseSpec(const std::string& text) {
  Spec s = { Spec::CONSTANT, 0, 0 };
  if (text == "ctl") { s.kind = Spec::CONTROL_D; return s; }
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
    default: return s.a;
  }
}

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
  const double gain_out = Arg("go", 1), gain_in = Arg("gi", 1), offset = Arg("off", 0);
  const bool go_env = Arg("go_env", 0) != 0, gi_env = Arg("gi_env", 0) != 0,
      off_env = Arg("off_env", 0) != 0;
  const double scale = Arg("scale", 1);
  const std::string tap = ArgS("tap", "bp");
  const std::string out_stage = ArgS("out", "lin");
  const double drive = Arg("drive", 1);
  unsigned noise_state = static_cast<unsigned>(Arg("seed", 1)) * 2654435761u | 1;

  // ctl=PATH: float32 pairs (d, G) per sample, the firmware's own controls,
  // in place of the envelope and d above. The upper-case parameters are specs
  // of them (ParseSpec), each taking over its stage when given: D the
  // filter's damping, N the input noise amount, L the small-signal loop gain
  // (the input gain becomes L / S'(offset), the output gain 1), OFF the
  // offset, VCA a gain after the output stage, DRIVE the output drive.
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
      has_drive = g_args.count("DRIVE");
  const Spec spec_n = ParseSpec(ArgS("N", "0")), spec_l = ParseSpec(ArgS("L", "0")),
      spec_off = ParseSpec(ArgS("OFF", "0")), spec_vca = ParseSpec(ArgS("VCA", "1")),
      spec_drive = ParseSpec(ArgS("DRIVE", "1"));

  const int length = has_controls ? static_cast<int>(controls.size() / 2)
                                  : static_cast<int>(duration * fs);
  std::vector<float> out(length);
  double bp = 0, lp = 0, dc_x = 0, dc_y = 0, level_at_key_up = 0;
  for (int i = 0; i < length; ++i) {
    const double t = i / fs;
    const double g = has_controls ? controls[2 * i + 1] : env.Value(t, &level_at_key_up);
    const double d_control = has_controls ? controls[2 * i]
        : d_start * pow(d_end / d_start, t / duration);
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
    double feedback = 0, feedback_tap = 0;
    if (shaper >= 0) {
      const double off = has_off ? Eval(spec_off, g, u, d) : offset * (off_env ? g : 1);
      double in_gain = gain_in * (gi_env ? g : 1), out_gain = gain_out * (go_env ? g : 1);
      if (has_l) {
        const double h = 1e-5;
        const double slope = (Shape(shaper, off + h) - Shape(shaper, off - h)) / (2 * h);
        if (fabs(slope) < 1e-3) { fprintf(stderr, "shaper flat at offset %g\n", off); return 1; }
        in_gain = Eval(spec_l, g, u, d) / slope;
        out_gain = 1;
      }
      const double x = off + in_gain * bp / scale;
      const double y = Shape(shaper, x);
      feedback = out_gain * d_c * scale * y;
      feedback_tap = scale * (y - Shape(shaper, off));
    }
    const double notch = in_noise + feedback - std::min(d, 2.0) * bp;
    lp += c * bp;
    bp += c * (notch - lp);
    double o;
    if (tap == "fb") {
      // One-pole DC blocker, 20 Hz.
      o = feedback_tap - dc_x + exp(-2 * M_PI * 20 / fs) * dc_y;
      dc_x = feedback_tap;
      dc_y = o;
    } else {
      o = tap == "lp" ? lp : bp;
      if (make_up) o /= make_up_gain;
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
