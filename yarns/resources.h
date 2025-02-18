// Copyright 2013 Emilie Gillet.
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
// Resources definitions.
//
// Automatically generated with:
// make resources


#ifndef YARNS_RESOURCES_H_
#define YARNS_RESOURCES_H_


#include "stmlib/stmlib.h"



namespace yarns {

typedef uint8_t ResourceId;

extern const char* const string_table[];

extern const uint16_t* const lookup_table_table[];

extern const int16_t* const lookup_table_signed_table[];

extern const char* const* const lookup_table_string_table[];

extern const int16_t* const waveform_table[];

extern const int16_t* const waveshaper_table[];

extern const uint32_t* const lookup_table_32_table[];

extern const int8_t* const lookup_table_8_table[];

extern const uint16_t* const char_table[];

extern const char str_dummy[];
extern const uint16_t lut_env_expo[];
extern const uint16_t lut_arpeggiator_patterns[];
extern const uint16_t lut_consonance[];
extern const uint16_t lut_clock_ratio_ticks[];
extern const uint16_t lut_svf_cutoff[];
extern const uint16_t lut_svf_damp[];
extern const uint16_t lut_svf_scale[];
extern const int16_t lut_fm_modulator_intervals[];
extern const char* const lut_fm_ratio_names[];
extern const char* const lut_clock_ratio_names[];
extern const int16_t wav_exponential[];
extern const int16_t wav_ring[];
extern const int16_t wav_steps[];
extern const int16_t wav_noise[];
extern const int16_t wav_sine[];
extern const int16_t wav_sizzle[];
extern const int16_t wav_bandlimited_comb_0[];
extern const int16_t wav_bandlimited_comb_1[];
extern const int16_t wav_bandlimited_comb_2[];
extern const int16_t wav_bandlimited_comb_3[];
extern const int16_t wav_bandlimited_comb_4[];
extern const int16_t wav_bandlimited_comb_5[];
extern const int16_t wav_bandlimited_comb_6[];
extern const int16_t wav_bandlimited_comb_7[];
extern const int16_t wav_bandlimited_comb_8[];
extern const int16_t wav_bandlimited_comb_9[];
extern const int16_t wav_bandlimited_comb_10[];
extern const int16_t wav_bandlimited_comb_11[];
extern const int16_t wav_bandlimited_comb_12[];
extern const int16_t wav_bandlimited_comb_13[];
extern const int16_t wav_bandlimited_comb_14[];
extern const int16_t ws_violent_overdrive[];
extern const int16_t ws_sine_fold[];
extern const int16_t ws_tri_fold[];
extern const uint32_t lut_lfo_increments[];
extern const uint32_t lut_portamento_increments[];
extern const uint32_t lut_envelope_phase_increments[];
extern const uint32_t lut_oscillator_increments[];
extern const uint32_t lut_euclidean[];
extern const int8_t lut_expo_slope_shift[];
extern const int8_t lut_fm_index_2x_upshifts[];
extern const uint16_t chr_characters[];
#define STR_DUMMY 0  // dummy
#define LUT_ENV_EXPO 0
#define LUT_ENV_EXPO_SIZE 257
#define LUT_ARPEGGIATOR_PATTERNS 1
#define LUT_ARPEGGIATOR_PATTERNS_SIZE 23
#define LUT_CONSONANCE 2
#define LUT_CONSONANCE_SIZE 1536
#define LUT_CLOCK_RATIO_TICKS 3
#define LUT_CLOCK_RATIO_TICKS_SIZE 32
#define LUT_SVF_CUTOFF 4
#define LUT_SVF_CUTOFF_SIZE 257
#define LUT_SVF_DAMP 5
#define LUT_SVF_DAMP_SIZE 257
#define LUT_SVF_SCALE 6
#define LUT_SVF_SCALE_SIZE 257
#define LUT_FM_MODULATOR_INTERVALS 0
#define LUT_FM_MODULATOR_INTERVALS_SIZE 26
#define LUT_FM_RATIO_NAMES 0
#define LUT_FM_RATIO_NAMES_SIZE 26
#define LUT_CLOCK_RATIO_NAMES 1
#define LUT_CLOCK_RATIO_NAMES_SIZE 32
#define WAV_EXPONENTIAL 0
#define WAV_EXPONENTIAL_SIZE 257
#define WAV_RING 1
#define WAV_RING_SIZE 257
#define WAV_STEPS 2
#define WAV_STEPS_SIZE 257
#define WAV_NOISE 3
#define WAV_NOISE_SIZE 257
#define WAV_SINE 4
#define WAV_SINE_SIZE 257
#define WAV_SIZZLE 5
#define WAV_SIZZLE_SIZE 257
#define WAV_BANDLIMITED_COMB_0 6
#define WAV_BANDLIMITED_COMB_0_SIZE 257
#define WAV_BANDLIMITED_COMB_1 7
#define WAV_BANDLIMITED_COMB_1_SIZE 257
#define WAV_BANDLIMITED_COMB_2 8
#define WAV_BANDLIMITED_COMB_2_SIZE 257
#define WAV_BANDLIMITED_COMB_3 9
#define WAV_BANDLIMITED_COMB_3_SIZE 257
#define WAV_BANDLIMITED_COMB_4 10
#define WAV_BANDLIMITED_COMB_4_SIZE 257
#define WAV_BANDLIMITED_COMB_5 11
#define WAV_BANDLIMITED_COMB_5_SIZE 257
#define WAV_BANDLIMITED_COMB_6 12
#define WAV_BANDLIMITED_COMB_6_SIZE 257
#define WAV_BANDLIMITED_COMB_7 13
#define WAV_BANDLIMITED_COMB_7_SIZE 257
#define WAV_BANDLIMITED_COMB_8 14
#define WAV_BANDLIMITED_COMB_8_SIZE 257
#define WAV_BANDLIMITED_COMB_9 15
#define WAV_BANDLIMITED_COMB_9_SIZE 257
#define WAV_BANDLIMITED_COMB_10 16
#define WAV_BANDLIMITED_COMB_10_SIZE 257
#define WAV_BANDLIMITED_COMB_11 17
#define WAV_BANDLIMITED_COMB_11_SIZE 257
#define WAV_BANDLIMITED_COMB_12 18
#define WAV_BANDLIMITED_COMB_12_SIZE 257
#define WAV_BANDLIMITED_COMB_13 19
#define WAV_BANDLIMITED_COMB_13_SIZE 257
#define WAV_BANDLIMITED_COMB_14 20
#define WAV_BANDLIMITED_COMB_14_SIZE 257
#define WS_VIOLENT_OVERDRIVE 0
#define WS_VIOLENT_OVERDRIVE_SIZE 257
#define WS_SINE_FOLD 1
#define WS_SINE_FOLD_SIZE 257
#define WS_TRI_FOLD 2
#define WS_TRI_FOLD_SIZE 257
#define LUT_LFO_INCREMENTS 0
#define LUT_LFO_INCREMENTS_SIZE 64
#define LUT_PORTAMENTO_INCREMENTS 1
#define LUT_PORTAMENTO_INCREMENTS_SIZE 128
#define LUT_ENVELOPE_PHASE_INCREMENTS 2
#define LUT_ENVELOPE_PHASE_INCREMENTS_SIZE 129
#define LUT_OSCILLATOR_INCREMENTS 3
#define LUT_OSCILLATOR_INCREMENTS_SIZE 97
#define LUT_EUCLIDEAN 4
#define LUT_EUCLIDEAN_SIZE 1024
#define LUT_EXPO_SLOPE_SHIFT 0
#define LUT_EXPO_SLOPE_SHIFT_SIZE 16
#define LUT_FM_INDEX_2X_UPSHIFTS 1
#define LUT_FM_INDEX_2X_UPSHIFTS_SIZE 26
#define CHR_CHARACTERS 0
#define CHR_CHARACTERS_SIZE 256

}  // namespace yarns

#endif  // YARNS_RESOURCES_H_
