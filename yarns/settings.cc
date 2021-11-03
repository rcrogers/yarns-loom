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

const char* const layout_values[LAYOUT_LAST] = {
  "1M", "2M", "4M", "2P", "4P", "2>", "4>", "8>", "4T", "4V", "31", "22", "21", "*2"
};

const char* const midi_out_mode_values[] = {
  "OFF", "THRU", "ARP/SEQ"
};

const char* const boolean_values[] = {
  "OFF", "ON"
};

const char* const voicing_allocation_mode_values[VOICE_ALLOCATION_MODE_LAST] = {
  "MONO", "POLY", "CYCLIC", "RANDOM", "VELO", "SORTED", "U1 UNISON",
  "U2 UNISON 2", "STEAL MOST RECENT", "NICE"
};

const char* const sequencer_arp_direction_values[ARPEGGIATOR_DIRECTION_LAST] = {
  "LINEAR", "BOUNCE", "RANDOM", "ROTATE", "SUBROTATE"
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
  "11 FM 1/1", "12 FM 1/2", "13 FM 1/3", "15 FM 1/5",
  "17 FM 1/7", "25 FM 2/5", "27 FM 2/7"
};

const char* const legato_mode_values[LEGATO_MODE_LAST] = {
  "OFF", "AUTO PORTAMENTO", "ON"
};

const char* const voicing_oscillator_mode_values[OSCILLATOR_MODE_LAST] = {
  "OFF", "DRONE", "ENVELOPED"
};

const char* const voicing_oscillator_shape_values[OSC_SHAPE_FM] = {
  "*\xA2 NOISE NOTCH SVF",
  "*\xA0 NOISE LOW-PASS SVF",
  "*^ NOISE BAND-PASS SVF",
  "*\xA1 NOISE HIGH-PASS SVF",
  "\x8C\xB0 LOW-PASS PULSE PHASE DISTORTION",
  "\x8C\xB1 PEAKING PULSE PHASE DISTORTION",
  "\x8C\xB2 BAND-PASS PULSE PHASE DISTORTION",
  "\x8C\xB3 HIGH-PASS PULSE PHASE DISTORTION",
  "\x88\xB0 LOW-PASS SAW PHASE DISTORTION",
  "\x88\xB1 PEAKING SAW PHASE DISTORTION",
  "\x88\xB2 BAND-PASS SAW PHASE DISTORTION",
  "\x88\xB3 HIGH-PASS SAW PHASE DISTORTION",
  "\x8C\xA0 PULSE LOW-PASS SVF",
  "\x88\xA0 SAW LOW-PASS SVF",
  "\x8CW PULSE WIDTH MOD",
  "\x88W SAW WIDTH MOD",
  "S$ SINE SYNC",
  "\x8C$ PULSE SYNC",
  "\x88$ SAW SYNC",
  "SF SINE FOLD",
  "^F TRIANGLE FOLD",
  "ST SINE TANH",
  "\x8E\x8E DIRAC COMB",
};

const char* const tremolo_shape_values[LFO_SHAPE_LAST] = {
  "/\\",
  "|\\",
  "/|",
  "\x8C_",
};

const char* const voicing_allocation_priority_values[] = {
  "LAST", "LOW", "HIGH", "FIRST"
};

const char* const trigger_shape_values[TRIGGER_SHAPE_LAST] = {
  "SQ", "LINEAR", "EXPO", "RING", "STEP", "BURST"
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

const uint8_t kVibratoSpeedMax = LUT_LFO_INCREMENTS_SIZE + LUT_CLOCK_RATIO_NAMES_SIZE - 1;
STATIC_ASSERT(kVibratoSpeedMax <= 127, overflow);

/* static */
const Setting Settings::settings_[] = {
  {
    "\x82""S", "SETUP MENU",
    SETTING_DOMAIN_MULTI, { 0, 0 },
    SETTING_UNIT_UINT8, 0, 0, NULL,
    0xff, 0xff,
  },
  {
    "\x82""O", "OSCILLATOR MENU",
    SETTING_DOMAIN_MULTI, { 0, 0 },
    SETTING_UNIT_UINT8, 0, 0, NULL,
    0xff, 0xff,
  },
  {
    "\x82""A", "AMPLITUDE MENU",
    SETTING_DOMAIN_MULTI, { 0, 0 },
    SETTING_UNIT_UINT8, 0, 0, NULL,
    0xff, 0xff,
  },
  {
    "LA", "LAYOUT",
    SETTING_DOMAIN_MULTI, { MULTI_LAYOUT, 0 },
    SETTING_UNIT_ENUMERATION, LAYOUT_MONO, LAYOUT_LAST - 1, layout_values,
    0xff, 1,
  },
  {
    "TM", "TEMPO",
    SETTING_DOMAIN_MULTI, { MULTI_CLOCK_TEMPO, 0 },
    SETTING_UNIT_TEMPO, TEMPO_EXTERNAL, 240, NULL,
    0xff, 2,
  },
  {
    "SW", "SWING",
    SETTING_DOMAIN_MULTI, { MULTI_CLOCK_SWING, 0 },
    SETTING_UNIT_UINT8, 0, 99, NULL,
    0xff, 3,
  },
  {
    "I/", "INPUT CLK DIV",
    SETTING_DOMAIN_MULTI, { MULTI_CLOCK_INPUT_DIVISION, 0 },
    SETTING_UNIT_UINT8, 1, 4, NULL,
    0xff, 0xff,
  },
  {
    "O/", "OUTPUT CLK RATIO",
    SETTING_DOMAIN_MULTI, { MULTI_CLOCK_OUTPUT_DIVISION, 0 },
    SETTING_UNIT_CLOCK_DIV, 0, LUT_CLOCK_RATIO_NAMES_SIZE - 1, NULL,
    0xff, 0,
  },
  {
    "B-", "BAR DURATION",
    SETTING_DOMAIN_MULTI, { MULTI_CLOCK_BAR_DURATION, 0 },
    SETTING_UNIT_BAR_DURATION, 0, kMaxBarDuration + 1, NULL,
    0xff, 0xff,
  },
  {
    "NU", "NUDGE 1ST TICK",
    SETTING_DOMAIN_MULTI, { MULTI_CLOCK_NUDGE_FIRST_TICK, 0 },
    SETTING_UNIT_ENUMERATION, 0, 1, boolean_values,
    0xff, 0xff,
  },
  {
    "MS", "CLOCK MANUAL START",
    SETTING_DOMAIN_MULTI, { MULTI_CLOCK_MANUAL_START, 0 },
    SETTING_UNIT_ENUMERATION, 0, 1, boolean_values,
    0xff, 0xff,
  },
  {
    "C>", "CLOCK OUTPUT",
    SETTING_DOMAIN_MULTI, { MULTI_CLOCK_OVERRIDE, 0 },
    SETTING_UNIT_ENUMERATION, 0, 1, boolean_values,
    0xff, 0xff,
  },
  {
    "CH", "CHANNEL",
    SETTING_DOMAIN_PART, { PART_MIDI_CHANNEL, 0 },
    SETTING_UNIT_MIDI_CHANNEL, 0, 16, NULL,
    0xff, 4,
  },
  {
    "N>", "NOTE>",
    SETTING_DOMAIN_PART, { PART_MIDI_MIN_NOTE, 0 },
    SETTING_UNIT_UINT8, 0, 127, NULL,
    16, 5,
  },
  {
    "N<", "NOTE<",
    SETTING_DOMAIN_PART, { PART_MIDI_MAX_NOTE, 0 },
    SETTING_UNIT_UINT8, 0, 127, NULL,
    17, 6,
  },
  {
    "NO", "NOTE",
    SETTING_DOMAIN_PART, { PART_MIDI_MIN_NOTE, PART_MIDI_MAX_NOTE },
    SETTING_UNIT_UINT8, 0, 127, NULL,
    0xff, 0xff,
  },
  {
    "V>", "VELO>",
    SETTING_DOMAIN_PART, { PART_MIDI_MIN_VELOCITY, 0 },
    SETTING_UNIT_UINT8, 0, 127, NULL,
    0xff, 0xff,
  },
  {
    "V<", "VELO<",
    SETTING_DOMAIN_PART, { PART_MIDI_MAX_VELOCITY, 0 },
    SETTING_UNIT_UINT8, 0, 127, NULL,
    0xff, 0xff,
  },
  {
    ">>", "OUTPUT MIDI MODE",
    SETTING_DOMAIN_PART, { PART_MIDI_OUT_MODE, 0 },
    SETTING_UNIT_ENUMERATION, 0, 2, midi_out_mode_values,
    0xff, 7,
  },
  {
    "IT", "INPUT TRANSPOSE OCTAVES",
    SETTING_DOMAIN_PART, { PART_MIDI_TRANSPOSE_OCTAVES, 0 },
    SETTING_UNIT_INT8, -4, 3, NULL,
    73, 0xff,
  },
  {
    "VO", "VOICING",
    SETTING_DOMAIN_PART, { PART_VOICING_ALLOCATION_MODE, 0 },
    SETTING_UNIT_ENUMERATION, 0, VOICE_ALLOCATION_MODE_LAST - 1,
    voicing_allocation_mode_values,
    18, 8,
  },
  {
    "NP", "NOTE PRIORITY",
    SETTING_DOMAIN_PART, { PART_VOICING_ALLOCATION_PRIORITY, 0 },
    SETTING_UNIT_ENUMERATION, 0, 3, voicing_allocation_priority_values,
    19, 9,
  },
  {
    "PO", "PORTAMENTO",
    SETTING_DOMAIN_PART, { PART_VOICING_PORTAMENTO, 0 },
    SETTING_UNIT_PORTAMENTO, 0, 127, NULL,
    5, 10,
  },
  {
    "LG", "LEGATO MODE",
    SETTING_DOMAIN_PART, { PART_VOICING_LEGATO_MODE, 0 },
    SETTING_UNIT_ENUMERATION, 0, LEGATO_MODE_LAST - 1, legato_mode_values,
    20, 11,
  },
  {
    "BR", "BEND RANGE",
    SETTING_DOMAIN_PART, { PART_VOICING_PITCH_BEND_RANGE, 0 },
    SETTING_UNIT_UINT8, 0, 24, NULL,
    21, 12,
  },
  {
    "VR", "VIBRATO AMP RANGE",
    SETTING_DOMAIN_PART, { PART_VOICING_VIBRATO_RANGE, 0 },
    SETTING_UNIT_UINT8, 0, 12, NULL,
    22, 13,
  },
  {
    "LF", "LFO RATE",
    SETTING_DOMAIN_PART, { PART_VOICING_LFO_RATE, 0 },
    SETTING_UNIT_VIBRATO_SPEED, 0, kVibratoSpeedMax, NULL,
    23, 14,
  },
  {
    "LT", "LFO SPREAD TYPES",
    SETTING_DOMAIN_PART, { PART_VOICING_LFO_SPREAD_TYPES, 0 },
    SETTING_UNIT_LFO_SPREAD, -64, 63, NULL,
    118, 0xff,
  },
  {
    "LV", "LFO SPREAD VOICES",
    SETTING_DOMAIN_PART, { PART_VOICING_LFO_SPREAD_VOICES, 0 },
    SETTING_UNIT_LFO_SPREAD, -64, 63, NULL,
    119, 0xff,
  },
  {
    "VB", "VIBRATO AMOUNT",
    SETTING_DOMAIN_PART, { PART_VOICING_VIBRATO_MOD, 0 },
    SETTING_UNIT_UINT8, 0, 127, NULL,
    1, 0xff,
  },
  {
    "TR", "TREMOLO DEPTH",
    SETTING_DOMAIN_PART, { PART_VOICING_TREMOLO_MOD, 0 },
    SETTING_UNIT_UINT8, 0, 127, NULL,
    93, 0xff,
  },
  {
    "TS", "TREMOLO SHAPE",
    SETTING_DOMAIN_PART, { PART_VOICING_TREMOLO_SHAPE, 0 },
    SETTING_UNIT_ENUMERATION, 0, LFO_SHAPE_LAST - 1, tremolo_shape_values,
    94, 0xff,
  },
  {
    "TT", "TRANSPOSE",
    SETTING_DOMAIN_PART, { PART_VOICING_TUNING_TRANSPOSE, 0 },
    SETTING_UNIT_INT8, -36, 36, NULL,
    24, 15,
  },
  {
    "TF", "FINE TUNING",
    SETTING_DOMAIN_PART, { PART_VOICING_TUNING_FINE, 0 },
    SETTING_UNIT_INT8, -64, 63, NULL,
    25, 16,
  },
  {
    "RN", "TUNING ROOT NOTE",
    SETTING_DOMAIN_PART, { PART_VOICING_TUNING_ROOT, 0 },
    SETTING_UNIT_ENUMERATION, 0, 11, note_values,
    26, 17,
  },
  {
    "TU", "TUNING SYSTEM",
    SETTING_DOMAIN_PART, { PART_VOICING_TUNING_SYSTEM, 0 },
    SETTING_UNIT_ENUMERATION, 0, TUNING_SYSTEM_LAST - 1,
    tuning_system_values,
    27, 18,
  },
  {
    "T-", "TRIG DURATION",
    SETTING_DOMAIN_PART, { PART_VOICING_TRIGGER_DURATION, 0 },
    SETTING_UNIT_UINT8, 1, 99, NULL,
    28, 19,
  },
  {
    "T*", "TRIG VELOCITY SCALE",
    SETTING_DOMAIN_PART, { PART_VOICING_TRIGGER_SCALE, 0 },
    SETTING_UNIT_ENUMERATION, 0, 1, boolean_values,
    29, 20,
  },
  {
    "T\x88", "TRIG SHAPE",
    SETTING_DOMAIN_PART, { PART_VOICING_TRIGGER_SHAPE, 0 },
    SETTING_UNIT_ENUMERATION, 0, TRIGGER_SHAPE_LAST - 1, trigger_shape_values,
    30, 21,
  },
  {
    "CV", "CV OUT",
    SETTING_DOMAIN_PART, { PART_VOICING_AUX_CV, 0 },
    SETTING_UNIT_ENUMERATION, 0, MOD_AUX_LAST - 1, voicing_aux_cv_values,
    31, 22,
  },
  {
    "3>", "CV OUT 3",
    SETTING_DOMAIN_PART, { PART_VOICING_AUX_CV, 0 },
    SETTING_UNIT_ENUMERATION, 0, MOD_AUX_LAST - 1, voicing_aux_cv_values,
    31, 22,
  },
  {
    "4>", "CV OUT 4",
    SETTING_DOMAIN_PART, { PART_VOICING_AUX_CV_2, 0 },
    SETTING_UNIT_ENUMERATION, 0, MOD_AUX_LAST - 1, voicing_aux_cv_values,
    72, 0xff,
  },
  {
    "OM", "OSC MODE",
    SETTING_DOMAIN_PART, { PART_VOICING_OSCILLATOR_MODE, 0 },
    SETTING_UNIT_ENUMERATION, 0, OSCILLATOR_MODE_LAST - 1, voicing_oscillator_mode_values,
    70, 0xff,
  },
  {
    "OS", "OSC SHAPE",
    SETTING_DOMAIN_PART, { PART_VOICING_OSCILLATOR_SHAPE, 0 },
    SETTING_UNIT_OSCILLATOR_SHAPE, 0, OSC_SHAPE_FM + LUT_FM_RATIO_NAMES_SIZE - 1, NULL,
    71, 23,
  },
  {
    "TI", "TIMBRE INIT",
    SETTING_DOMAIN_PART, { PART_VOICING_TIMBRE_INIT, 0 },
    SETTING_UNIT_UINT8, 0, 127, NULL,
    82, 0xff,
  },
  {
    "TL", "TIMBRE LFO MOD",
    SETTING_DOMAIN_PART, { PART_VOICING_TIMBRE_MOD_LFO, 0 },
    SETTING_UNIT_UINT8, 0, 127, NULL,
    83, 0xff,
  },
  {
    "TE", "TIMBRE ENV MOD",
    SETTING_DOMAIN_PART, { PART_VOICING_TIMBRE_MOD_ENVELOPE, 0 },
    SETTING_UNIT_INT8, -64, 63, NULL,
    90, 0xff,
  },
  {
    "TV", "TIMBRE VEL MOD",
    SETTING_DOMAIN_PART, { PART_VOICING_TIMBRE_MOD_VELOCITY, 0 },
    SETTING_UNIT_INT8, -64, 63, NULL,
    91, 0xff,
  },
  {
    "PV", "PEAK VEL MOD",
    SETTING_DOMAIN_PART, { PART_VOICING_ENV_PEAK_MOD_VELOCITY, 0 },
    SETTING_UNIT_INT8, -64, 63, NULL,
    92, 0xff,
  },
  {
    "AI", "ATTACK INIT",
    SETTING_DOMAIN_PART, { PART_VOICING_ENV_INIT_ATTACK, 0 },
    SETTING_UNIT_UINT8, 0, 127, NULL,
    77, 0xff,
  },
  {
    "DI", "DECAY INIT",
    SETTING_DOMAIN_PART, { PART_VOICING_ENV_INIT_DECAY, 0 },
    SETTING_UNIT_UINT8, 0, 127, NULL,
    78, 0xff,
  },
  {
    "SI", "SUSTAIN INIT",
    SETTING_DOMAIN_PART, { PART_VOICING_ENV_INIT_SUSTAIN, 0 },
    SETTING_UNIT_UINT8, 0, 127, NULL,
    79, 0xff,
  },
  {
    "RI", "RELEASE INIT",
    SETTING_DOMAIN_PART, { PART_VOICING_ENV_INIT_RELEASE, 0 },
    SETTING_UNIT_UINT8, 0, 127, NULL,
    80, 0xff,
  },
  {
    "AM", "ATTACK MOD VEL",
    SETTING_DOMAIN_PART, { PART_VOICING_ENV_MOD_ATTACK, 0 },
    SETTING_UNIT_INT8, -64, 63, NULL,
    86, 0xff,
  },
  {
    "DM", "DECAY MOD VEL",
    SETTING_DOMAIN_PART, { PART_VOICING_ENV_MOD_DECAY, 0 },
    SETTING_UNIT_INT8, -64, 63, NULL,
    87, 0xff,
  },
  {
    "SM", "SUSTAIN MOD VEL",
    SETTING_DOMAIN_PART, { PART_VOICING_ENV_MOD_SUSTAIN, 0 },
    SETTING_UNIT_INT8, -64, 63, NULL,
    88, 0xff,
  },
  {
    "RM", "RELEASE MOD VEL",
    SETTING_DOMAIN_PART, { PART_VOICING_ENV_MOD_RELEASE, 0 },
    SETTING_UNIT_INT8, -64, 63, NULL,
    89, 0xff,
  },
  {
    "C/", "CLK RATIO OUT-IN",
    SETTING_DOMAIN_PART, { PART_SEQUENCER_CLOCK_DIVISION, 0 },
    SETTING_UNIT_CLOCK_DIV, 0, LUT_CLOCK_RATIO_NAMES_SIZE - 1, NULL,
    102, 24,
  },
  {
    "G-", "GATE LENGTH",
    SETTING_DOMAIN_PART, { PART_SEQUENCER_GATE_LENGTH, 0 },
    SETTING_UNIT_UINT8, 1, 48, NULL,
    103, 25,
  },
  {
    "AR", "ARP RANGE",
    SETTING_DOMAIN_PART, { PART_SEQUENCER_ARP_RANGE, 0 },
    SETTING_UNIT_INDEX, 0, 3, NULL,
    104, 26,
  },
  {
    "AD", "ARP DIRECTION",
    SETTING_DOMAIN_PART, { PART_SEQUENCER_ARP_DIRECTION, 0 },
    SETTING_UNIT_ENUMERATION, 0, ARPEGGIATOR_DIRECTION_LAST - 1,
    sequencer_arp_direction_values,
    105, 27,
  },
  {
    "AP", "ARP PATTERN",
    SETTING_DOMAIN_PART, { PART_SEQUENCER_ARP_PATTERN, 0 },
    SETTING_UNIT_ARP_PATTERN, 0, LUT_ARPEGGIATOR_PATTERNS_SIZE, NULL,
    106, 28,
  },
  {
    "RP", "RHYTHMIC PATTERN",
    SETTING_DOMAIN_PART, { PART_SEQUENCER_ARP_PATTERN, 0 },
    SETTING_UNIT_ARP_PATTERN, 0, LUT_ARPEGGIATOR_PATTERNS_SIZE, NULL,
    0xff, 0xff,
  },
  {
    "E-", "EUCLIDEAN LENGTH",
    SETTING_DOMAIN_PART, { PART_SEQUENCER_EUCLIDEAN_LENGTH, 0 },
    SETTING_UNIT_UINT8, 0, 31, NULL,
    107, 29,
  },
  {
    "EF", "EUCLIDEAN FILL",
    SETTING_DOMAIN_PART, { PART_SEQUENCER_EUCLIDEAN_FILL, 0 },
    SETTING_UNIT_UINT8, 0, 31, NULL,
    108, 30,
  },
  {
    "ER", "EUCLIDEAN ROTATE",
    SETTING_DOMAIN_PART, { PART_SEQUENCER_EUCLIDEAN_ROTATE, 0 },
    SETTING_UNIT_UINT8, 0, 31, NULL,
    109, 31,
  },
  {
    "PM", "PLAY MODE",
    SETTING_DOMAIN_PART, { PART_MIDI_PLAY_MODE, 0 },
    SETTING_UNIT_ENUMERATION, 0, PLAY_MODE_LAST - 1, sequencer_play_mode_values,
    114, 0xff,
  },
  {
    "SI", "SEQ INPUT RESPONSE",
    SETTING_DOMAIN_PART, { PART_MIDI_INPUT_RESPONSE, 0 },
    SETTING_UNIT_ENUMERATION, 0, SEQUENCER_INPUT_RESPONSE_LAST - 1, sequencer_input_response_values,
    76, 0xff,
  },
  {
    "SM", "SEQ MODE",
    SETTING_DOMAIN_PART, { PART_SEQUENCER_CLOCK_QUANTIZATION, 0 },
    SETTING_UNIT_ENUMERATION, 0, 1, sequencer_clock_quantization_values,
    75, 0xff,
  },
  {
    "L-", "LOOP LENGTH",
    SETTING_DOMAIN_PART, { PART_SEQUENCER_LOOP_LENGTH, 0 },
    SETTING_UNIT_LOOP_LENGTH, 0, 7, NULL,
    84, 0xff,
  },
  {
    "HM", "HOLD PEDAL MODE",
    SETTING_DOMAIN_PART, { PART_MIDI_SUSTAIN_MODE, 0 },
    SETTING_UNIT_ENUMERATION, 0, SUSTAIN_MODE_LAST - 1, sustain_mode_values,
    74, 0xff,
  },
  {
    "HP", "HOLD PEDAL POLARITY",
    SETTING_DOMAIN_PART, { PART_MIDI_SUSTAIN_POLARITY, 0 },
    SETTING_UNIT_ENUMERATION, 0, 1, hold_pedal_polarity_values,
    85, 0xff,
  },
  {
    "RC", "REMOTE CONTROL CHANNEL",
    SETTING_DOMAIN_MULTI, { MULTI_REMOTE_CONTROL_CHANNEL, 0 },
    SETTING_UNIT_MIDI_CHANNEL_OFF, 0, 16, NULL,
    0xff, 0xff,
  },
  {
    "T*", "TUNING FACTOR",
    SETTING_DOMAIN_PART, { PART_VOICING_TUNING_FACTOR, 0 },
    SETTING_UNIT_ENUMERATION, 0, 13,
    tuning_factor_values,
    0xff, 0xff,
  }
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

void Settings::Print(const Setting& setting, uint8_t value, char* buffer) const {
  switch (setting.unit) {
    case SETTING_UNIT_UINT8:
      PrintInteger(buffer, value);
      break;
      
    case SETTING_UNIT_INT8:
      PrintSignedInteger(buffer, value);
      break;
    
    case SETTING_UNIT_INDEX:
      PrintInteger(buffer, value + 1);
      break;
      
    case SETTING_UNIT_BAR_DURATION:
      if (value <= kMaxBarDuration) {
        PrintInteger(buffer, value);
      } else {
        strcpy(buffer, "oo");
      }
      break;

    case SETTING_UNIT_TEMPO:
      if (value == TEMPO_EXTERNAL) {
        strcpy(buffer, "EXTERNAL");
      } else {
        PrintInteger(buffer, value);
      }
      break;

    case SETTING_UNIT_MIDI_CHANNEL:
      if (value == 0x10) {
        strcpy(buffer, "ALL");
      } else {
        PrintInteger(buffer, value + 1);
      }
      break;

    case SETTING_UNIT_MIDI_CHANNEL_OFF:
      if (value == 0x00) {
        strcpy(buffer, "OFF");
      } else {
        PrintInteger(buffer, value);
      }
      break;

    case SETTING_UNIT_CLOCK_DIV:
      strcpy(buffer, lut_clock_ratio_names[value]);
      break;
    
    case SETTING_UNIT_VIBRATO_SPEED:
      if (value < LUT_LFO_INCREMENTS_SIZE) {
        PrintInteger(buffer, LUT_LFO_INCREMENTS_SIZE - value - 1);
      } else {
        Print(settings_[SETTING_SEQUENCER_CLOCK_DIVISION], value - LUT_LFO_INCREMENTS_SIZE, buffer);
      }
      if (buffer[0] == ' ') {
        buffer[0] = value < LUT_LFO_INCREMENTS_SIZE ? 'F' : ' ';
      }
      break;
      
    case SETTING_UNIT_PORTAMENTO:
    {
      uint8_t split_point = LUT_PORTAMENTO_INCREMENTS_SIZE >> 1;
      if (value < split_point) {
        PrintInteger(buffer, split_point - value);
      } else {
        PrintInteger(buffer, value - split_point);
      }
      if (buffer[0] == ' ') {
        buffer[0] = value < split_point ? 'T' : 'R';
      }
      break;
    }
      
    case SETTING_UNIT_ENUMERATION:
      strcpy(buffer, setting.values[value]);
      break;

    case SETTING_UNIT_ARP_PATTERN:
      if (value == 0) {
        strcpy(buffer, "SEQUENCER");
      } else {
        PrintInteger(buffer, value);
      }
      break;

    case SETTING_UNIT_LOOP_LENGTH:
      PrintInteger(buffer, 1 << value);
      break;

    case SETTING_UNIT_OSCILLATOR_SHAPE:
      if (value >= OSC_SHAPE_FM) {
        strcpy(buffer, lut_fm_ratio_names[value - OSC_SHAPE_FM]);
      } else {
        strcpy(buffer, voicing_oscillator_shape_values[value]);
      }
      break;

    case SETTING_UNIT_LFO_SPREAD:
      PrintInteger(buffer, abs((int8_t) value));
      if (buffer[0] == ' ') {
        buffer[0] = ((int8_t) value) < 0 ? 'F' : 'P';
      }
      break;
      
    default:
      strcpy(buffer, "??");
  }
}

/* static */
void Settings::PrintInteger(char* buffer, uint8_t number) {
  buffer[1] = '0' + (number % 10);
  number /= 10;
  buffer[0] = number ? '0' + (number % 10) : ' ';
  number /= 10;
  buffer[2] = '\0';
}

/* static */
void Settings::PrintSignedInteger(char* buffer, int8_t number) {
  if (number >= 0) {
    PrintInteger(buffer, number);
    if (buffer[0] == ' ') {
      buffer[0] = '+';
    }
  } else if (number > -10){
    PrintInteger(buffer, -number);
    buffer[0] = '-';
  } else {
    PrintInteger(buffer, -number);
    buffer[2] = ' ';
    buffer[3] = '-';
    buffer[4] = buffer[0];
    buffer[5] = buffer[1];
    buffer[6] = '\0';
  }
}

/* extern */
Settings setting_defs;

}  // namespace yarns
