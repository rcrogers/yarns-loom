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

extern const char* string_table[];

extern const uint16_t* lookup_table_table[];

extern const int16_t* lookup_table_signed_table[];

extern const char* const* lookup_table_string_table[];

extern const int16_t* waveform_table[];

extern const int16_t* waveshaper_table[];

extern const uint32_t* lookup_table_32_table[];

extern const uint16_t* char_table[];

extern const char str_dummy[];
extern const uint16_t lut_env_expo[];
extern const uint16_t lut_arpeggiator_patterns[];
extern const uint16_t lut_consonance[];
extern const uint16_t lut_clock_ratio_ticks[];
extern const uint16_t lut_svf_cutoff[];
extern const uint16_t lut_svf_damp[];
extern const uint16_t lut_svf_scale[];
extern const int16_t lut_scale_pythagorean[];
extern const int16_t lut_scale_1_4_eb[];
extern const int16_t lut_scale_1_4_e[];
extern const int16_t lut_scale_1_4_ea[];
extern const int16_t lut_scale_bhairav[];
extern const int16_t lut_scale_gunakri[];
extern const int16_t lut_scale_marwa[];
extern const int16_t lut_scale_shree[];
extern const int16_t lut_scale_purvi[];
extern const int16_t lut_scale_bilawal[];
extern const int16_t lut_scale_yaman[];
extern const int16_t lut_scale_kafi[];
extern const int16_t lut_scale_bhimpalasree[];
extern const int16_t lut_scale_darbari[];
extern const int16_t lut_scale_rageshree[];
extern const int16_t lut_scale_khamaj[];
extern const int16_t lut_scale_mimal[];
extern const int16_t lut_scale_parameshwari[];
extern const int16_t lut_scale_rangeshwari[];
extern const int16_t lut_scale_gangeshwari[];
extern const int16_t lut_scale_kameshwari[];
extern const int16_t lut_scale_pa__kafi[];
extern const int16_t lut_scale_natbhairav[];
extern const int16_t lut_scale_m_kauns[];
extern const int16_t lut_scale_bairagi[];
extern const int16_t lut_scale_b_todi[];
extern const int16_t lut_scale_chandradeep[];
extern const int16_t lut_scale_kaushik_todi[];
extern const int16_t lut_scale_jogeshwari[];
extern const int16_t lut_fm_modulator_intervals[];
extern const char* const lut_fm_ratio_names[];
extern const char* const lut_clock_ratio_names[];
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
extern const uint32_t lut_oscillator_increments[];
extern const uint32_t lut_euclidean[];
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
#define LUT_SCALE_PYTHAGOREAN 0
#define LUT_SCALE_PYTHAGOREAN_SIZE 12
#define LUT_SCALE_1_4_EB 1
#define LUT_SCALE_1_4_EB_SIZE 12
#define LUT_SCALE_1_4_E 2
#define LUT_SCALE_1_4_E_SIZE 12
#define LUT_SCALE_1_4_EA 3
#define LUT_SCALE_1_4_EA_SIZE 12
#define LUT_SCALE_BHAIRAV 4
#define LUT_SCALE_BHAIRAV_SIZE 12
#define LUT_SCALE_GUNAKRI 5
#define LUT_SCALE_GUNAKRI_SIZE 12
#define LUT_SCALE_MARWA 6
#define LUT_SCALE_MARWA_SIZE 12
#define LUT_SCALE_SHREE 7
#define LUT_SCALE_SHREE_SIZE 12
#define LUT_SCALE_PURVI 8
#define LUT_SCALE_PURVI_SIZE 12
#define LUT_SCALE_BILAWAL 9
#define LUT_SCALE_BILAWAL_SIZE 12
#define LUT_SCALE_YAMAN 10
#define LUT_SCALE_YAMAN_SIZE 12
#define LUT_SCALE_KAFI 11
#define LUT_SCALE_KAFI_SIZE 12
#define LUT_SCALE_BHIMPALASREE 12
#define LUT_SCALE_BHIMPALASREE_SIZE 12
#define LUT_SCALE_DARBARI 13
#define LUT_SCALE_DARBARI_SIZE 12
#define LUT_SCALE_BAGESHREE 14
#define LUT_SCALE_BAGESHREE_SIZE 12
#define LUT_SCALE_RAGESHREE 15
#define LUT_SCALE_RAGESHREE_SIZE 12
#define LUT_SCALE_KHAMAJ 16
#define LUT_SCALE_KHAMAJ_SIZE 12
#define LUT_SCALE_MIMAL 17
#define LUT_SCALE_MIMAL_SIZE 12
#define LUT_SCALE_PARAMESHWARI 18
#define LUT_SCALE_PARAMESHWARI_SIZE 12
#define LUT_SCALE_RANGESHWARI 19
#define LUT_SCALE_RANGESHWARI_SIZE 12
#define LUT_SCALE_GANGESHWARI 20
#define LUT_SCALE_GANGESHWARI_SIZE 12
#define LUT_SCALE_KAMESHWARI 21
#define LUT_SCALE_KAMESHWARI_SIZE 12
#define LUT_SCALE_PA__KAFI 22
#define LUT_SCALE_PA__KAFI_SIZE 12
#define LUT_SCALE_NATBHAIRAV 23
#define LUT_SCALE_NATBHAIRAV_SIZE 12
#define LUT_SCALE_M_KAUNS 24
#define LUT_SCALE_M_KAUNS_SIZE 12
#define LUT_SCALE_BAIRAGI 25
#define LUT_SCALE_BAIRAGI_SIZE 12
#define LUT_SCALE_B_TODI 26
#define LUT_SCALE_B_TODI_SIZE 12
#define LUT_SCALE_CHANDRADEEP 27
#define LUT_SCALE_CHANDRADEEP_SIZE 12
#define LUT_SCALE_KAUSHIK_TODI 28
#define LUT_SCALE_KAUSHIK_TODI_SIZE 12
#define LUT_SCALE_JOGESHWARI 29
#define LUT_SCALE_JOGESHWARI_SIZE 12
#define LUT_SCALE_RASIA 30
#define LUT_SCALE_RASIA_SIZE 12
#define LUT_FM_MODULATOR_INTERVALS 31
#define LUT_FM_MODULATOR_INTERVALS_SIZE 26
#define LUT_FM_RATIO_NAMES 0
#define LUT_FM_RATIO_NAMES_SIZE 26
#define LUT_CLOCK_RATIO_NAMES 1
#define LUT_CLOCK_RATIO_NAMES_SIZE 32
#define WAV_SINE 0
#define WAV_SINE_SIZE 257
#define WAV_SIZZLE 1
#define WAV_SIZZLE_SIZE 257
#define WAV_BANDLIMITED_COMB_0 2
#define WAV_BANDLIMITED_COMB_0_SIZE 257
#define WAV_BANDLIMITED_COMB_1 3
#define WAV_BANDLIMITED_COMB_1_SIZE 257
#define WAV_BANDLIMITED_COMB_2 4
#define WAV_BANDLIMITED_COMB_2_SIZE 257
#define WAV_BANDLIMITED_COMB_3 5
#define WAV_BANDLIMITED_COMB_3_SIZE 257
#define WAV_BANDLIMITED_COMB_4 6
#define WAV_BANDLIMITED_COMB_4_SIZE 257
#define WAV_BANDLIMITED_COMB_5 7
#define WAV_BANDLIMITED_COMB_5_SIZE 257
#define WAV_BANDLIMITED_COMB_6 8
#define WAV_BANDLIMITED_COMB_6_SIZE 257
#define WAV_BANDLIMITED_COMB_7 9
#define WAV_BANDLIMITED_COMB_7_SIZE 257
#define WAV_BANDLIMITED_COMB_8 10
#define WAV_BANDLIMITED_COMB_8_SIZE 257
#define WAV_BANDLIMITED_COMB_9 11
#define WAV_BANDLIMITED_COMB_9_SIZE 257
#define WAV_BANDLIMITED_COMB_10 12
#define WAV_BANDLIMITED_COMB_10_SIZE 257
#define WAV_BANDLIMITED_COMB_11 13
#define WAV_BANDLIMITED_COMB_11_SIZE 257
#define WAV_BANDLIMITED_COMB_12 14
#define WAV_BANDLIMITED_COMB_12_SIZE 257
#define WAV_BANDLIMITED_COMB_13 15
#define WAV_BANDLIMITED_COMB_13_SIZE 257
#define WAV_BANDLIMITED_COMB_14 16
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
#define LUT_OSCILLATOR_INCREMENTS 2
#define LUT_OSCILLATOR_INCREMENTS_SIZE 97
#define LUT_EUCLIDEAN 3
#define LUT_EUCLIDEAN_SIZE 1024
#define CHR_CHARACTERS 0
#define CHR_CHARACTERS_SIZE 256

}  // namespace yarns

#endif  // YARNS_RESOURCES_H_
