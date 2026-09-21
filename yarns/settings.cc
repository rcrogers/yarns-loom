// Copyright 2013 Emilie Gillet.
// Copyright 2020 Chris Rogers.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Parameter definitions.

#include "yarns/settings.h"
#include "yarns/resources.h"
#include "yarns/oscillator.h"

#include "yarns/multi.h"
#include "yarns/part.h"

#include <cstring>

namespace yarns {

// Unbounded and then asserted: declaring it [LAYOUT_LAST] would zero-fill a
// missing name into a NULL the display dereferences.
const char* const layout_values[] = {
  "1M 1 MONO PART",
  "2M 2 MONO PARTS",
  "4M 4 MONO PARTS",
  "2P 2-VOICE POLY",
  "4P 4-VOICE POLY",
  "2> 2-VOICE POLYCHAINED",
  "4> 4-VOICE POLYCHAINED",
  "8> 8-VOICE POLYCHAINED",
  "4T 4 TRIGGERS",
  "4V 4 CONTROL VOLTAGES",
  "31 3-VOICE POLY + 1 MONO PART",
  "22 2-VOICE POLY + 2 MONO PARTS",
  "21 2-VOICE POLY + 1 MONO PART",
  "*2 PARAPHONIC + 2 MONO PARTS + 1 GATE",
  "3M 3 MONO PARTS",
  "*1 PARAPHONIC + 1 MONO PART",
};
typedef char layout_values_needs_one_name_per_layout[
    (sizeof(layout_values) / sizeof(layout_values[0]) == LAYOUT_LAST)
        ? 1 : -1];

const char* const control_change_mode_values[CONTROL_CHANGE_MODE_LAST] = {
  "OFF",
  "ABSOLUTE 0-127",
  "RD RELATIVE DIRECT",
  "RS RELATIVE SCALED",
};

const char* const midi_out_mode_values[] = {
  "OFF", "THRU", "ARP/SEQ"
};

const char* const boolean_values[] = {
  "OFF", "ON"
};

const char* const voicing_allocation_mode_values[POLY_MODE_LAST] = {
  "MONOPHONIC",
  "sM STEAL LOWEST PRIORITY RELEASE MUTE",
  "CYCLIC",
  "RANDOM",
  "VELOCITY",
  "PRIORITY ORDER",
  "UR UNISON RELEASE REASSIGN",
  "UM UNISON RELEASE MUTE",
  "SM STEAL HIGHEST PRIORITY RELEASE MUTE",
  "sR STEAL LOWEST PRIORITY RELEASE REASSIGN",
  "SR STEAL HIGHEST PRIORITY RELEASE REASSIGN",
};

const char* const sequencer_arp_direction_values[ARPEGGIATOR_DIRECTION_LAST] = {
  "LINEAR", "BOUNCE", "RANDOM", "JUMP", "GRID"
};

const char* const voicing_aux_cv_values[MOD_AUX_LAST] = {
  "VELOCITY",
  "MODULATION",
  "AFTERTOUCH",
  "BREATH",
  "PEDAL",
  "BEND",
  "VIBRATO LFO",
  "LFO",
  "ENVELOPE",
  // lut_fm_ratio_names[0],
  // lut_fm_ratio_names[1],
  // lut_fm_ratio_names[2],
  // lut_fm_ratio_names[3],
  // lut_fm_ratio_names[4],
  // lut_fm_ratio_names[5],
  // lut_fm_ratio_names[6],
  "11 FM 1/1", "21 FM 2/1", "31 FM 3/1", "51 FM 5/1",
  "71 FM 7/1", "52 FM 5/2", "72 FM 7/2"
};

const char* const voicing_oscillator_mode_values[OSCILLATOR_MODE_LAST] = {
  "OFF", "DRONE", "ENVELOPED"
};

const char* const voicing_oscillator_shape_values[] = {
  "*\xA2 NOISE NOTCH SVF",
  "*\xA0 NOISE LOW-PASS SVF",
  "*^ NOISE BAND-PASS SVF",
  "*\xA1 NOISE HIGH-PASS SVF",
  "WH WHISTLE",
  "P\xA0 LOW-PASS PING",
  "P^ BAND-PASS PING",
  "P\xA1 HIGH-PASS PING",
  "\x8C\xA0 PULSE LOW-PASS SVF",
  "\x88\xA0 SAW LOW-PASS SVF",
  "\x8C\xB0 LOW-PASS PULSE PHASE DISTORTION",
  "\x8C\xB1 PEAKING PULSE PHASE DISTORTION",
  "\x8C\xB2 BAND-PASS PULSE PHASE DISTORTION",
  "\x8C\xB3 HIGH-PASS PULSE PHASE DISTORTION",
  "\x88\xB0 LOW-PASS SAW PHASE DISTORTION",
  "\x88\xB1 PEAKING SAW PHASE DISTORTION",
  "\x88\xB2 BAND-PASS SAW PHASE DISTORTION",
  "\x88\xB3 HIGH-PASS SAW PHASE DISTORTION",
  "SW SINE WIDTH MOD",
  "\x8CW PULSE WIDTH MOD",
  "\x88W SAW WIDTH MOD",
  "\x88\x8C SAW-PULSE MORPH",
  "S$ SINE SYNC",
  // "^$ TRIANGLE SYNC",
  "\x8C$ PULSE SYNC",
  "\x88$ SAW SYNC",
  // "SF SINE FOLD",
  // "^F TRIANGLE FOLD",
  "\x8E\x8E DIRAC COMB",
  "ST SINE TANH",
  "SX SINE EXPONENTIAL",
  "^^ TRI THRU TRI",
  "S^ SINE THRU TRI",
  "e^ EXP THRU TRI",
  "^\xC3 BIASED TRI THRU TRI",
  "S\xC3 BIASED SINE THRU TRI",
  "e\xC3 BIASED EXP THRU TRI",
  "^s TRI THRU SINE",
  "Ss SINE THRU SINE",
  "es EXP THRU SINE",
  "^\xC2 BIASED TRI THRU SINE",
  "S\xC2 BIASED SINE THRU SINE",
  "e\xC2 BIASED EXP THRU SINE",
  "^e TRI THRU EXP",
  "Se SINE THRU EXP",
  "ee EXP THRU EXP",
  "^\xC4 BIASED TRI THRU EXP",
  "S\xC4 BIASED SINE THRU EXP",
  "e\xC4 BIASED EXP THRU EXP",
};
STATIC_ASSERT(
  OSC_SHAPE_FM == sizeof(voicing_oscillator_shape_values) / sizeof(const char*),
  osc
);

const char* const lfo_shape_values[LFO_SHAPE_LAST] = {
  "/\\ TRIANGLE",
  "|\\ DOWN SAW",
  "/| UP SAW",
  "\x8C_ SQUARE",
  "R\\ LINEAR RANDOM",
  "Re EXPO SLEW RANDOM",
  "R\xC5 STEPPED RANDOM",
  "R* NOISE",
};

const char* const voicing_allocation_priority_values[] = {
  "LAST", "LOW", "HIGH", "FIRST"
};

const char* const note_values[12] = {
  "C ", "Db", "D", "Eb", "E ", "F ", "Gb", "G ", "Ab", "A ", "Bb", "B "
};

const char* const tuning_system_values[TUNING_SYSTEM_LAST] = {
  "EQUAL TEMPERAMENT",
  "JUST INTONATION",
  "PYTHAGOREAN",
  "EB 1/4",
  "E 1/4",
  "EA 1/4",
  "01 BHAIRAV",
  "02 GUNAKRI",
  "03 MARWA",
  "04 SHREE",
  "05 PURVI",
  "06 BILAWAL",
  "07 YAMAN",
  "08 KAFI",
  "09 BHIMPALASREE",
  "10 DARBARI",
  "11 BAGESHREE",
  "12 RAGESHREE",
  "13 KHAMAJ",
  "14 MI MAL",
  "15 PARAMESHWARI",
  "16 RANGESHWARI",
  "17 GANGESHWARI",
  "18 KAMESHWARI",
  "19 PA KAFI",
  "20 NATBHAIRAV",
  "21 M.KAUNS",
  "22 BAIRAGI",
  "23 B.TODI",
  "24 CHANDRADEEP",
  "25 KAUSHIK TODI",
  "26 JOGESHWARI",
  "27 RASIA",
  "CUSTOM"
};

const char* const sequencer_play_mode_values[PLAY_MODE_LAST] = {
  "MANUAL",
  "ARPEGGIATOR",
  "SEQUENCER",
};

const char* const sequencer_clock_quantization_values[] = {
  "LOOP",
  "STEP"
};

const char* const sequencer_input_response_values[SEQUENCER_INPUT_RESPONSE_LAST] = {
  "OFF", "TRANSPOSE", "REPLACE", "DIRECT"
};

const char* const sustain_mode_values[SUSTAIN_MODE_LAST] = {
  "OFF",
  "SUSTAIN",
  "SOSTENUTO",
  "LATCH",
  "MOMENTARY LATCH",
  "CLUTCH",
  "FILTER",
};

const char* const hold_pedal_polarity_values[] = {
  "- NEG YAMAHA ROLAND",
  "+ POS CASIO KORG",
};

const char* const tuning_factor_values[] = {
  "OFF",
  "0 ",
  "18 1/8",
  "14 1/4",
  "38 3/8",
  "12 1/2",
  "58 5/8",
  "34 3/4",
  "78 7/8",
  "1  1/1",
  "54 5/4",
  "32 3/2",
  "2  2/1",
  "ALPHA"
};

/* static */
const Setting Settings::settings_[] = {
  {
    "SETUP MENU", NULL,
    { 0, 0 }, 0, 0,
    "\x82""S", SETTING_DOMAIN_MULTI, SETTING_UNIT_UINT8,
    0xff, 0xff,
  },
  {
    "OSCILLATOR MENU", NULL,
    { 0, 0 }, 0, 0,
    "\x82""O", SETTING_DOMAIN_MULTI, SETTING_UNIT_UINT8,
    0xff, 0xff,
  },
  {
    "AMPLITUDE MENU", NULL,
    { 0, 0 }, 0, 0,
    "\x82""A", SETTING_DOMAIN_MULTI, SETTING_UNIT_UINT8,
    0xff, 0xff,
  },
  {
    "LAYOUT", layout_values,
    { MULTI_LAYOUT, 0 }, LAYOUT_MONO, LAYOUT_LAST - 1,
    "LA", SETTING_DOMAIN_MULTI, SETTING_UNIT_ENUMERATION,
    0xff, 1,
  },
  {
    "TEMPO", NULL,
    { MULTI_CLOCK_TEMPO, 0 }, TEMPO_EXTERNAL, 240,
    "TM", SETTING_DOMAIN_MULTI, SETTING_UNIT_TEMPO,
    0xff, 2,
  },
  {
    "SWING", NULL,
    { MULTI_CLOCK_SWING, 0 }, -63, 63,
    "SW", SETTING_DOMAIN_MULTI, SETTING_UNIT_INT8,
    0xff, 3,
  },
  {
    "INPUT CLOCK DIV", NULL,
    { MULTI_CLOCK_INPUT_DIVISION, 0 }, 1, 4,
    "I/", SETTING_DOMAIN_MULTI, SETTING_UNIT_UINT8,
    0xff, 0xff,
  },
  {
    "OUTPUT CLOCK RATIO OUT/IN", NULL,
    { MULTI_CLOCK_OUTPUT_DIVISION, 0 }, 0, LUT_CLOCK_RATIO_NAMES_SIZE - 1,
    "O/", SETTING_DOMAIN_MULTI, SETTING_UNIT_CLOCK_DIV,
    0xff, 0,
  },
  {
    "CLOCK OFFSET", NULL,
    { MULTI_CLOCK_OFFSET, 0 }, -64, 63,
    "C+", SETTING_DOMAIN_MULTI, SETTING_UNIT_INT8,
    0xff, 0xff,
  },
  {
    "BAR DURATION", NULL,
    { MULTI_CLOCK_BAR_DURATION, 0 }, 0, kMaxBarDuration + 1,
    "B-", SETTING_DOMAIN_MULTI, SETTING_UNIT_BAR_DURATION,
    0xff, 0xff,
  },
  {
    "NUDGE 1ST TICK", boolean_values,
    { MULTI_CLOCK_NUDGE_FIRST_TICK, 0 }, 0, 1,
    "NU", SETTING_DOMAIN_MULTI, SETTING_UNIT_ENUMERATION,
    0xff, 0xff,
  },
  {
    "CLOCK MANUAL START", boolean_values,
    { MULTI_CLOCK_MANUAL_START, 0 }, 0, 1,
    "MS", SETTING_DOMAIN_MULTI, SETTING_UNIT_ENUMERATION,
    0xff, 0xff,
  },
  {
    "CLOCK OUTPUT", boolean_values,
    { MULTI_CLOCK_OVERRIDE, 0 }, 0, 1,
    "C>", SETTING_DOMAIN_MULTI, SETTING_UNIT_ENUMERATION,
    0xff, 0xff,
  },
  {
    "CONTROL CHANGE MODE", control_change_mode_values,
    { MULTI_CONTROL_CHANGE_MODE, 0 }, 0, CONTROL_CHANGE_MODE_LAST - 1,
    "CC", SETTING_DOMAIN_MULTI, SETTING_UNIT_ENUMERATION,
    0xff, 0xff,
  },
  {
    "CHANNEL", NULL,
    { PART_MIDI_CHANNEL, 0 }, 0, 16,
    "CH", SETTING_DOMAIN_PART, SETTING_UNIT_MIDI_CHANNEL_LAST_OMNI,
    0xff, 4,
  },
  {
    "NOTE>", NULL,
    { PART_MIDI_MIN_NOTE, 0 }, 0, 127,
    "N>", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    16, 5,
  },
  {
    "NOTE<", NULL,
    { PART_MIDI_MAX_NOTE, 0 }, 0, 127,
    "N<", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    17, 6,
  },
  {
    "NOTE", NULL,
    { PART_MIDI_MIN_NOTE, PART_MIDI_MAX_NOTE }, 0, 127,
    "NO", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    0xff, 0xff,
  },
  {
    "VELO>", NULL,
    { PART_MIDI_MIN_VELOCITY, 0 }, 0, 127,
    "V>", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    0xff, 0xff,
  },
  {
    "VELO<", NULL,
    { PART_MIDI_MAX_VELOCITY, 0 }, 0, 127,
    "V<", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    0xff, 0xff,
  },
  {
    "OUTPUT MIDI MODE", midi_out_mode_values,
    { PART_MIDI_OUT_MODE, 0 }, 0, 2,
    ">>", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    0xff, 7,
  },
  {
    "INPUT TRANSPOSE OCTAVES", NULL,
    { PART_MIDI_TRANSPOSE_OCTAVES, 0 }, -4, 3,
    "IT", SETTING_DOMAIN_PART, SETTING_UNIT_INT8,
    73, 0xff,
  },
  {
    "VOICING", voicing_allocation_mode_values,
    { PART_VOICING_ALLOCATION_MODE, 0 }, 0, POLY_MODE_LAST - 1,
    "VO", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    18, 8,
  },
  {
    "NOTE PRIORITY", voicing_allocation_priority_values,
    { PART_VOICING_ALLOCATION_PRIORITY, 0 }, 0, 3,
    "NP", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    19, 9,
  },
  {
    "PORTAMENTO", NULL,
    { PART_VOICING_PORTAMENTO, 0 }, 1, 127,
    "PO", SETTING_DOMAIN_PART, SETTING_UNIT_PORTAMENTO,
    5, 10,
  },
  {
    "LEGATO RETRIGGER", boolean_values,
    { PART_VOICING_LEGATO_RETRIGGER, 0 }, 0, 1,
    "LG", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    20, 11,
  },
  {
    "PORTAMENTO LEGATO ONLY", boolean_values,
    { PART_VOICING_PORTAMENTO_LEGATO_ONLY, 0 }, 0, 1,
    "PL", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    32, 0xff,
  },
  {
    "BEND RANGE", NULL,
    { PART_VOICING_PITCH_BEND_RANGE, 0 }, 0, 24,
    "BR", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    21, 12,
  },
  {
    "VIBRATO AMP RANGE", NULL,
    { PART_VOICING_VIBRATO_RANGE, 0 }, 0, 12,
    "VR", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    22, 13,
  },
  {
    "LFO RATE", NULL,
    { PART_VOICING_LFO_RATE, 0 }, 0, 127,
    "LF", SETTING_DOMAIN_PART, SETTING_UNIT_LFO_RATE,
    23, 14,
  },
  {
    "LFO SPREAD TYPES", NULL,
    { PART_VOICING_LFO_SPREAD_TYPES, 0 }, -64, 63,
    "LT", SETTING_DOMAIN_PART, SETTING_UNIT_LFO_SPREAD,
    118, 0xff,
  },
  {
    "LFO SPREAD VOICES", NULL,
    { PART_VOICING_LFO_SPREAD_VOICES, 0 }, -64, 63,
    "LV", SETTING_DOMAIN_PART, SETTING_UNIT_LFO_SPREAD,
    119, 0xff,
  },
  {
    "VIBRATO AMOUNT", NULL,
    { PART_VOICING_VIBRATO_MOD, 0 }, 0, 127,
    "VB", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    1, 0xff,
  },
  {
    "TREMOLO DEPTH", NULL,
    { PART_VOICING_TREMOLO_MOD, 0 }, 0, 127,
    "TR", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    93, 0xff,
  },
  {
    "VIBRATO SHAPE", lfo_shape_values,
    { PART_VOICING_VIBRATO_SHAPE, 0 }, 0, LFO_SHAPE_LAST - 1,
    "VS", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    95, 0xff,
  },
  {
    "TIMBRE LFO SHAPE", lfo_shape_values,
    { PART_VOICING_TIMBRE_LFO_SHAPE, 0 }, 0, LFO_SHAPE_LAST - 1,
    "LS", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    81, 0xff,
  },
  {
    "TREMOLO SHAPE", lfo_shape_values,
    { PART_VOICING_TREMOLO_SHAPE, 0 }, 0, LFO_SHAPE_LAST - 1,
    "TS", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    94, 0xff,
  },
  {
    "TRANSPOSE", NULL,
    { PART_VOICING_TUNING_TRANSPOSE, 0 }, -32, 31,
    "TT", SETTING_DOMAIN_PART, SETTING_UNIT_INT8,
    24, 15,
  },
  {
    "FINE TUNING", NULL,
    { PART_VOICING_TUNING_FINE, 0 }, -64, 63,
    "TF", SETTING_DOMAIN_PART, SETTING_UNIT_INT8,
    25, 16,
  },
  {
    "TUNING ROOT NOTE", note_values,
    { PART_VOICING_TUNING_ROOT, 0 }, 0, 11,
    "RN", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    26, 17,
  },
  {
    "TUNING SYSTEM", tuning_system_values,
    { PART_VOICING_TUNING_SYSTEM, 0 }, 0, TUNING_SYSTEM_LAST - 1,
    "TU", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    27, 18,
  },
  {
    "", NULL,
    { 0, 0 }, 0, 0,
    "", SETTING_DOMAIN_MULTI, SETTING_UNIT_UINT8,
    0xff, 0xff,
  },
  {
    "", NULL,
    { 0, 0 }, 0, 0,
    "", SETTING_DOMAIN_MULTI, SETTING_UNIT_UINT8,
    0xff, 0xff,
  },
  {
    "", NULL,
    { 0, 0 }, 0, 0,
    "", SETTING_DOMAIN_MULTI, SETTING_UNIT_UINT8,
    0xff, 0xff,
  },
  {
    "CV OUT", voicing_aux_cv_values,
    { PART_VOICING_AUX_CV, 0 }, 0, MOD_AUX_LAST - 1,
    "CV", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    31, 22,
  },
  {
    "CV OUT 3", voicing_aux_cv_values,
    { PART_VOICING_AUX_CV, 0 }, 0, MOD_AUX_LAST - 1,
    "3>", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    31, 22,
  },
  {
    "CV OUT 4", voicing_aux_cv_values,
    { PART_VOICING_AUX_CV_2, 0 }, 0, MOD_AUX_LAST - 1,
    "4>", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    72, 0xff,
  },
  {
    "OSC MODE", voicing_oscillator_mode_values,
    { PART_VOICING_OSCILLATOR_MODE, 0 }, 0, OSCILLATOR_MODE_LAST - 1,
    "OM", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    70, 0xff,
  },
  {
    "OSC SHAPE", NULL,
    { PART_VOICING_OSCILLATOR_SHAPE, 0 }, 0, OSC_SHAPE_FM + LUT_FM_RATIO_NAMES_SIZE - 1,
    "OS", SETTING_DOMAIN_PART, SETTING_UNIT_OSCILLATOR_SHAPE,
    71, 23,
  },
  {
    "TIMBRE INIT", NULL,
    { PART_VOICING_TIMBRE_INIT, 0 }, 0, 127,
    "TI", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    82, 0xff,
  },
  {
    "TIMBRE LFO MOD", NULL,
    { PART_VOICING_TIMBRE_MOD_LFO, 0 }, 0, 127,
    "TL", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    83, 0xff,
  },
  {
    "TIMBRE ENV MOD", NULL,
    { PART_VOICING_TIMBRE_MOD_ENVELOPE, 0 }, -64, 63,
    "TE", SETTING_DOMAIN_PART, SETTING_UNIT_INT8,
    90, 0xff,
  },
  {
    "TIMBRE VEL MOD", NULL,
    { PART_VOICING_TIMBRE_MOD_VELOCITY, 0 }, -64, 63,
    "TV", SETTING_DOMAIN_PART, SETTING_UNIT_INT8,
    91, 0xff,
  },
  {
    "PEAK VEL MOD", NULL,
    { PART_VOICING_ENV_PEAK_MOD_VELOCITY, 0 }, -64, 63,
    "PE", SETTING_DOMAIN_PART, SETTING_UNIT_INT8,
    92, 0xff,
  },
  {
    "ATTACK INIT", NULL,
    { PART_VOICING_ENV_INIT_ATTACK, 0 }, 0, 127,
    "AI", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    77, 0xff,
  },
  {
    "DECAY INIT", NULL,
    { PART_VOICING_ENV_INIT_DECAY, 0 }, 0, 127,
    "DI", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    78, 0xff,
  },
  {
    "SUSTAIN INIT", NULL,
    { PART_VOICING_ENV_INIT_SUSTAIN, 0 }, 0, 127,
    "SI", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    79, 0xff,
  },
  {
    "RELEASE INIT", NULL,
    { PART_VOICING_ENV_INIT_RELEASE, 0 }, 0, 127,
    "RI", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    80, 0xff,
  },
  {
    "ATTACK MOD VEL", NULL,
    { PART_VOICING_ENV_MOD_ATTACK, 0 }, -64, 63,
    "AM", SETTING_DOMAIN_PART, SETTING_UNIT_INT8,
    86, 0xff,
  },
  {
    "DECAY MOD VEL", NULL,
    { PART_VOICING_ENV_MOD_DECAY, 0 }, -64, 63,
    "DM", SETTING_DOMAIN_PART, SETTING_UNIT_INT8,
    87, 0xff,
  },
  {
    "SUSTAIN MOD VEL", NULL,
    { PART_VOICING_ENV_MOD_SUSTAIN, 0 }, -64, 63,
    "SM", SETTING_DOMAIN_PART, SETTING_UNIT_INT8,
    88, 0xff,
  },
  {
    "RELEASE MOD VEL", NULL,
    { PART_VOICING_ENV_MOD_RELEASE, 0 }, -64, 63,
    "RM", SETTING_DOMAIN_PART, SETTING_UNIT_INT8,
    89, 0xff,
  },
  {
    "CLOCK RATIO OUT/IN", NULL,
    { PART_SEQUENCER_CLOCK_DIVISION, 0 }, 0, LUT_CLOCK_RATIO_NAMES_SIZE - 1,
    "C/", SETTING_DOMAIN_PART, SETTING_UNIT_CLOCK_DIV,
    102, 24,
  },
  {
    "GATE LENGTH", NULL,
    { PART_SEQUENCER_GATE_LENGTH, 0 }, 0, 63,
    "G-", SETTING_DOMAIN_PART, SETTING_UNIT_INDEX,
    103, 25,
  },
  {
    "ARP RANGE", NULL,
    { PART_SEQUENCER_ARP_RANGE, 0 }, 0, 3,
    "AR", SETTING_DOMAIN_PART, SETTING_UNIT_INDEX,
    104, 26,
  },
  {
    "ARP DIRECTION", sequencer_arp_direction_values,
    { PART_SEQUENCER_ARP_DIRECTION, 0 }, 0, ARPEGGIATOR_DIRECTION_LAST - 1,
    "AD", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    105, 27,
  },
  {
    "ARP PATTERN", NULL,
    { PART_SEQUENCER_ARP_PATTERN, 0 }, 0, 31,
    "AP", SETTING_DOMAIN_PART, SETTING_UNIT_ARP_PATTERN,
    106, 28,
  },
  {
    "RHYTHMIC PATTERN", NULL,
    { PART_SEQUENCER_ARP_PATTERN, 0 }, 0, 31,
    "RP", SETTING_DOMAIN_PART, SETTING_UNIT_ARP_PATTERN,
    0xff, 0xff,
  },
  {
    "EUCLIDEAN LENGTH", NULL,
    { PART_SEQUENCER_EUCLIDEAN_LENGTH, 0 }, 0, 31,
    "E-", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    107, 29,
  },
  {
    "EUCLIDEAN FILL", NULL,
    { PART_SEQUENCER_EUCLIDEAN_FILL, 0 }, 0, 31,
    "EF", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    108, 30,
  },
  {
    "STEP OFFSET", NULL,
    { PART_SEQUENCER_STEP_OFFSET, 0 }, 0, 31,
    "SO", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    109, 31,
  },
  {
    "PLAY MODE", sequencer_play_mode_values,
    { PART_MIDI_PLAY_MODE, 0 }, 0, PLAY_MODE_LAST - 1,
    "PM", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    114, 0xff,
  },
  {
    "SEQ INPUT RESPONSE", sequencer_input_response_values,
    { PART_MIDI_INPUT_RESPONSE, 0 }, 0, SEQUENCER_INPUT_RESPONSE_LAST - 1,
    "SI", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    76, 0xff,
  },
  {
    "SEQ MODE", sequencer_clock_quantization_values,
    { PART_SEQUENCER_CLOCK_QUANTIZATION, 0 }, 0, 1,
    "SM", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    75, 0xff,
  },
  {
    "LOOP LENGTH", NULL,
    { PART_SEQUENCER_LOOP_LENGTH, 0 }, 0, 7,
    "L-", SETTING_DOMAIN_PART, SETTING_UNIT_LOOP_LENGTH,
    84, 0xff,
  },
  {
    "HOLD PEDAL MODE", sustain_mode_values,
    { PART_MIDI_SUSTAIN_MODE, 0 }, 0, SUSTAIN_MODE_LAST - 1,
    "HM", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    74, 0xff,
  },
  {
    "HOLD PEDAL POLARITY", hold_pedal_polarity_values,
    { PART_MIDI_SUSTAIN_POLARITY, 0 }, 0, 1,
    "HP", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    85, 0xff,
  },
  {
    "REMOTE CONTROL CHANNEL", NULL,
    { MULTI_REMOTE_CONTROL_CHANNEL, 0 }, 0, 16,
    "RC", SETTING_DOMAIN_MULTI, SETTING_UNIT_MIDI_CHANNEL_FIRST_OFF,
    0xff, 0xff,
  },
  {
    "TUNING FACTOR", tuning_factor_values,
    { PART_VOICING_TUNING_FACTOR, 0 }, 0, 13,
    "T*", SETTING_DOMAIN_PART, SETTING_UNIT_ENUMERATION,
    0xff, 0xff,
  },
  {
    "PORTAMENTO MOD VEL", NULL,
    { PART_VOICING_PORTAMENTO_MOD_VELOCITY, 0 }, -64, 63,
    "PV", SETTING_DOMAIN_PART, SETTING_UNIT_INT8,
    33, 0xff,
  },
  {
    "EXCITER AMOUNT INIT", NULL,
    { PART_VOICING_CHIFF_AMOUNT, 0 }, 0, 127,
    "\xC6""I", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    0xff, 0xff,
  },
  {
    "EXCITER AMOUNT MOD VEL", NULL,
    { PART_VOICING_CHIFF_AMOUNT_MOD_VELOCITY, 0 }, -64, 63,
    "\xC6""V", SETTING_DOMAIN_PART, SETTING_UNIT_INT8,
    0xff, 0xff,
  },
  {
    "EXCITER DURATION INIT", NULL,
    { PART_VOICING_CHIFF_DURATION, 0 }, 0, 127,
    "\xC7""I", SETTING_DOMAIN_PART, SETTING_UNIT_UINT8,
    0xff, 0xff,
  },
  {
    "EXCITER DURATION MOD VEL", NULL,
    { PART_VOICING_CHIFF_DURATION_MOD_VELOCITY, 0 }, -64, 63,
    "\xC7""V", SETTING_DOMAIN_PART, SETTING_UNIT_INT8,
    0xff, 0xff,
  },
};

void Settings::Init() {
  // Build tables used to convert from a CC to a parameter number.
  std::fill(&part_cc_map[0], &part_cc_map[128], 0xff);
  std::fill(&remote_control_cc_map[0], &remote_control_cc_map[128], 0xff);
  
  for (uint8_t i = 0; i < SETTING_LAST; ++i) {
    const Setting& setting = settings_[i];
    if (setting.part_cc != 0xff) {
      if (setting.domain != SETTING_DOMAIN_PART) while (1);
      part_cc_map[setting.part_cc] = i;
    }
    if (setting.remote_control_cc != 0xff) {
      uint8_t num_instances = setting.domain == SETTING_DOMAIN_PART ? 4 : 1;
      for (uint8_t j = 0; j < num_instances; ++j) {
        remote_control_cc_map[setting.remote_control_cc + j * 32] = i;
      }
    }
  }
}

// Return prefix character for display
char Settings::Print(const Setting& setting, uint8_t value, char* buffer) const {
  switch (setting.unit) {
    case SETTING_UNIT_UINT8:
      return PrintInteger(buffer, value);
      
    case SETTING_UNIT_INT8:
      if (&setting == &setting_defs.get(SETTING_CLOCK_SWING)) {
        int8_t swing = static_cast<int8_t>(value);
        if (value == 0) {
          strcpy(buffer, "OFF");
          return '\0';
        } else {
          return PrintInteger(buffer, abs(swing), swing ? (swing < 0 ? 'o' : 'e') : '\0');
        }
      } else {
        return PrintSignedInteger(buffer, value);
      }
    
    case SETTING_UNIT_INDEX:
      return PrintInteger(buffer, value + 1);
      
    case SETTING_UNIT_BAR_DURATION:
      if (value <= kMaxBarDuration) {
        return PrintInteger(buffer, value);
      } else {
        strcpy(buffer, "oo");
        return '\0';
      }

    case SETTING_UNIT_TEMPO:
      if (value == TEMPO_EXTERNAL) {
        strcpy(buffer, "EXTERNAL");
        return '\0';
      } else {
        return PrintInteger(buffer, value);
      }

    case SETTING_UNIT_MIDI_CHANNEL_LAST_OMNI:
      if (value == kMidiChannelOmni) {
        strcpy(buffer, "ALL");
        return '\0';
      } else {
        return PrintInteger(buffer, value + 1);
      }

    case SETTING_UNIT_MIDI_CHANNEL_FIRST_OFF:
      if (value == 0x00) {
        strcpy(buffer, "OFF");
        return '\0';
      } else {
        return PrintInteger(buffer, value);
      }

    case SETTING_UNIT_CLOCK_DIV:
      strcpy(buffer, lut_clock_ratio_names[value]);
      return '\0';
    
    case SETTING_UNIT_LFO_RATE:
      if (value < 64) {
        STATIC_ASSERT(LUT_CLOCK_RATIO_NAMES_SIZE == 32, ratios); // Allows an easy bit shift
        return Print(settings_[SETTING_SEQUENCER_CLOCK_DIVISION], (64 - value - 1) >> 1, buffer);
      } else {
        return PrintInteger(buffer, value + 1 - 64, 'F');
      }
      
    case SETTING_UNIT_PORTAMENTO:
    {
      // Exclude Interpolate88 guard entry from split point.
      const uint8_t split_point = LUT_PORTAMENTO_INCREMENTS_SIZE - 1;
      if (value == split_point) {
        strcpy(buffer, "OFF");
        return '\0';
      } else if (value < split_point) {
        return PrintInteger(buffer, split_point - value, 'T');
      } else {
        return PrintInteger(buffer, value - split_point, 'R');
      }
    }
      
    case SETTING_UNIT_ENUMERATION:
      strcpy(buffer, setting.values[value]);
      return '\0';

    case SETTING_UNIT_ARP_PATTERN:
      {
        int8_t pattern = LUT_ARPEGGIATOR_PATTERNS_SIZE - value;
        if (pattern > 0) { // Pattern-driven, left side
          return PrintInteger(buffer, pattern, 'P');
        } else { // Sequencer-driven, right side
          return PrintInteger(buffer, abs(pattern), 'S');
        }
      }

    case SETTING_UNIT_LOOP_LENGTH:
      return PrintInteger(buffer, 1 << value);

    case SETTING_UNIT_OSCILLATOR_SHAPE:
      if (value >= OSC_SHAPE_FM) {
        strcpy(buffer, lut_fm_ratio_names[value - OSC_SHAPE_FM]);
      } else {
        strcpy(buffer, voicing_oscillator_shape_values[value]);
      }
      return '\0';

    case SETTING_UNIT_LFO_SPREAD: {
      int8_t spread = value;
      bool dephase = spread < 0;
      if (dephase) spread++;
      return PrintInteger(buffer, abs(spread), dephase ? 'P' : 'F');
    }
      
    default:
      strcpy(buffer, "??");
      return '\0';
  }
}

/* static */
char Settings::PrintInteger(char* buffer, uint8_t number, char prefix) {
  buffer[1] = '0' + (number % 10);
  number /= 10;
  buffer[0] = number ? '0' + (number % 10) : ' ';
  number /= 10;
  buffer[2] = '\0';
  return number ? '0' + (number % 10) : prefix;
}

/* static */
char Settings::PrintSignedInteger(char* buffer, int8_t number) {
  return PrintInteger(buffer, abs(number), number < 0 ? '-' : '+');
}

/* extern */
Settings setting_defs;

}  // namespace yarns
