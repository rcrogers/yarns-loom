// Copyright 2012 Emilie Gillet.
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

#include "yarns/envelope.h"

#include "yarns/multi.h"

#include "stmlib/dsp/dsp.h"

#pragma GCC diagnostic error "-Wsign-conversion"
#pragma GCC diagnostic error "-Wsign-compare"
// #pragma GCC diagnostic error "-Wconversion"

namespace yarns {

using namespace stmlib;

/*
Edge cases when a manually started segment has an off-nominal delta to target:
1. Wrong direction (skip)
  - Affects: ATTACK, DECAY
    - "High attack interrupted by low attack"
      - NoteOn with high peak
      - ATTACK interrupted by early NoteOff
      - EARLY_RELEASE interrupted by NoteOn with a peak level below current value
    - Inverted decay, if peak level is below sustain level
    - In general, delta from actual value to target has different sign than delta from nominal start value to target
      - I.e., we seem to have already passed the target
  - Solution
    - ATTACK has wrong direction: skip to DECAY
    - DECAY has wrong direction: this is more ambiguous
      - Should we just disallow peak < sustain?
2. Value too far from target (prelude)
  - Affects: DECAY, RELEASE
    - Primary case: NoteOff before SUSTAIN segment has started
    - Can also apply to decay start, e.g.:
      - "High attack interrupted by low attack" causes ATTACK to skip to DECAY
      - ATTACK skips to decay
      - DECAY is now further from sustain level than expected
  - Solution: prelude
    - Populate the segment's expo slopes
    - Extend the segment's initial/steepest slope backward in time
    - Give each Motion a new counter of prelude samples remaining
      - While these count down, use the first expo slope value only
      - See calculation for release_prelude_.samples_left
3. Value too close to target
  - Affects: ATTACK, RELEASE
    - NoteOff from below sustain level
    - NoteOn when above minimum level
  - Solution: truncate beginning of phase
    - Want to skip forward in phase
    - Translating "value too close" to a phase is nonlinear due to expo slope
      - Use LUT that translates an expo value to a phase?
*/

void Envelope::Init(int32_t value) {
  value_ = value;
  phase_ = 0;
  output_bias_ = 0;
  segment_ = ENV_SEGMENT_DEAD;
  expo_ = NULL;
  for (uint8_t i = 0; i < kNumEdges; ++i) {
    edges_[i].slope = 0;
    edges_[i].samples = 0;
  }
  // Trigger(ENV_SEGMENT_DEAD);
}

void Envelope::NoteOff() {
  if (segment_ < ENV_SEGMENT_RELEASE) {
    Trigger(ENV_SEGMENT_RELEASE, true);
  }
}

void Envelope::NoteOn(
  ADSR& adsr,
  int32_t min_target, int32_t max_target // Actual bounds, 16-bit signed
) {
  // NB: min_target changes between notes IFF calibration/settings change
  int16_t scale = max_target - min_target; // TODO overflow?
  min_target <<= 16;
  int32_t peak = min_target + scale * adsr.peak;
  int32_t sustain = min_target + scale * adsr.sustain;

  attack_.nominal_offset = min_target;
  attack_.target = peak;
  attack_.phase_increment = adsr.attack;
  attack_.scale = 0;
  attack_.offset = 0;

  decay_.nominal_offset = peak;
  decay_.target = sustain;
  decay_.phase_increment = adsr.decay;
  decay_.scale = 0;
  decay_.offset = 0;

  release_.nominal_offset = sustain;
  release_.target = min_target;
  release_.phase_increment = adsr.release;
  release_.scale = 0;
  release_.offset = 0;

  // TODO could precompute decay/release slopes here, don't have to wait for them to arrive.  downside is that decay can be skipped (release cannot).  Trigger would need to know whether to use the precomputed slope or compute a new one.

  if (segment_ > ENV_SEGMENT_SUSTAIN) {
    // multi.PrintInt32E(value_);
    Trigger(ENV_SEGMENT_ATTACK, true);
  }
}

int16_t Envelope::tremolo(uint16_t strength) const {
  int32_t relative_value = (value_ - release_.target) >> 16;
  return relative_value * -strength >> 16;
}
/*
#define DOWNSHIFT_SIGNED_1BIT(value) \
  ((value) >= 0 ? ((value) >> 1) : ((value) + 1) >> 1)

#define DIFF_DOWNSHIFT(a, b) \
  (DOWNSHIFT_SIGNED_1BIT(a) - DOWNSHIFT_SIGNED_1BIT(b))
*/

// #define DIFF_DOWNSHIFT(a, b) ((a / 2) - (b / 2))

void Envelope::Trigger(EnvelopeSegment segment, bool manual) {
  segment_ = segment;
  switch (segment) {
    case ENV_SEGMENT_ATTACK : expo_ = &attack_  ; break;
    case ENV_SEGMENT_DECAY  : expo_ = &decay_   ; break;
    case ENV_SEGMENT_RELEASE: expo_ = &release_ ; break;
    default:
      expo_ = NULL;
      return;
  }

  int32_t nominal_scale = SatSub(expo_->target, expo_->nominal_offset);
  int32_t measured_scale = SatSub(expo_->target, value_);
  bool movement_expected = nominal_scale != 0;

  // Skip segment if there is a direction disagreement or nowhere to go
  if (
    // Already at target (important to skip because 0 disrupts direction checks)
    !measured_scale || (
      // The segment is supposed to have a direction
      movement_expected && (
        // It doesn't agree with the actual direction
        (nominal_scale > 0) != (measured_scale > 0)
      )
      // Cases: NoteOn during release from above peak level
      // TODO are direction skips good in case of polarity reversals? only if there are non-attack cases
    )
  ) {
    // Seeing some of these after a totally normal ADS to 0 sustain, probably indicating underflow.  How to avoid overshoot?
    // multi.PrintDebugByte(0x0F + (segment << 4));
    return Trigger(static_cast<EnvelopeSegment>(segment + 1), manual);
  }

  // multi.PrintDebugByte(0x09 + (segment << 4));

  // Determine X and Y distance to travel during this segment
  
  if ( // Closer to target than expected
    movement_expected && (
      // NB: we already ruled out direction disagreement
      abs(measured_scale) < abs(nominal_scale)
    )
  ) {
    // Cases: NoteOn during release (of same polarity); NoteOff from below sustain level during attack 

    // multi.PrintDebugByte(0x0B + (segment << 4));

    // Keep the nominal segment steepness
    expo_->scale = nominal_scale;
    expo_->offset = expo_->nominal_offset;

    // Pre-advance X to reflect Y already traveled
    int32_t progress_amount = SatSub(value_, expo_->nominal_offset);
    int32_t progress_fraction = progress_amount / (expo_->scale >> 16);
    phase_ = Interpolate88(lut_env_inverse_expo, stmlib::ClipU16(progress_fraction));
    phase_ <<= 16;
  } else {
    // We're at least as far as expected (possibly farther).  Make the curve as steep as needed to cover the Y distance in the expected time
    // Cases: NoteOff during attack/decay from between sustain/peak levels; NoteOn during release of opposite polarity (hi timbre); normal well-adjusted segments
    // multi.PrintDebugByte(0x0A + (segment << 4));
    expo_->scale = measured_scale;
    expo_->offset = value_;
    phase_ = 0;
  }

  size_t segment_samples = (UINT32_MAX - phase_) / expo_->phase_increment;
  // uint32_t edge_phase_fraction = UINT32_MAX >> kEdgeBits;
  // int32_t simulated_value = expo_->offset;
  int32_t last_scaled_expo = 0;
  for (uint32_t i = 0; i < kNumEdges; ++i) {
    edges_[i].samples = segment_samples >> kEdgeBits; // TODO

    // TODO def getting overshoot

    // uint32_t edge_end_phase = (UINT32_MAX >> kEdgeBits) * (i + 1);
    uint8_t edge_end_phase = ((i + 1) << (8 - kEdgeBits)) - 1;
    // uint16_t expo_fraction = Interpolate88(lut_env_expo, edge_end_phase);
    uint16_t expo_fraction = lut_env_expo[edge_end_phase];  

    // Super accurate, but 2 mults + 1 div per slice
    // int32_t scaled_expo = (expo_->scale >> 12) * (expo_fraction >> 4);
    // int32_t edge_target = expo_->offset + scaled_expo;
    // int32_t edge_delta = SatSub(edge_target, simulated_value);
    // edges_[i].slope = (edge_delta / static_cast<int32_t>(edges_[i].samples));
    // simulated_value = edge_target;
    // // simulated_value += edges_[i].slope * static_cast<int32_t>(edges_[i].samples);
    
    // This is way more accurate than shifts, but requires 2 multiplications per slice.
    int32_t scaled_expo = (expo_->scale >> 12) * (expo_fraction >> 4);
    int32_t edge_delta = SatSub(scaled_expo, last_scaled_expo);
    // TODO constrain to min/max from noteon?
    edges_[i].slope = (edge_delta >> 16) * static_cast<int32_t>(expo_->phase_increment >> 16);
    last_scaled_expo = scaled_expo;

    // a compromise with 1 mult per slice: LUT of "slope ratios" in 3.13 or 2.14 format
  
    // If the curve function has a constant 2nd derivative, it should be possible to generate a single scaled number that is *added* to the slope each slice, e.g. -x² + 2x
    // https://claude.ai/chat/2f2083f0-11b9-42ea-a62c-f85652cda175
  }
  current_edge_ = phase_ >> (32 - kEdgeBits);
}


/* Render TODO

Try copying expo slope to local

Separate inline buffer of length 4-8?
Write to output buffer 4 samples at a time?

Try outputting slopes instead, then computing a running total
https://claude.ai/chat/127cae75-04b9-4d95-87ac-d01114bd7cf3
Complication: shifting slopes before summing will cause error
Running total must be 32-bit, slope vector must be 32-bit, output buffer can be 16-bit
*/

template<size_t BUFFER_SIZE>
void Envelope::RenderSamples(
  stmlib::RingBuffer<int16_t, BUFFER_SIZE>* buffer, 
  int32_t new_output_bias, // TODO should be 16-bit?
  size_t render_samples_needed
) {
  new_output_bias = 0; // TODO

  int32_t current_output_value = value_ + output_bias_;

  if (!expo_) {
    // TODO this probably would cause noise by jumping to the new bias
    // int32_t slope = (DIFF_DOWNSHIFT(new_output_bias, output_bias_) / static_cast<int32_t>(render_samples_needed)) << 1;
    int32_t slope = 0;
    while (render_samples_needed--) {
      current_output_value += slope;
      buffer->Overwrite(current_output_value >> 16);
    }
    output_bias_ = new_output_bias;
    return;
  }
  output_bias_ = new_output_bias;

  // Linear interpolation over this render cycle
  Edge& edge = edges_[current_edge_];
  size_t samples_rendered = std::min(edge.samples, render_samples_needed);
  int32_t slope = edge.slope;
  // TODO sum with bias slope? may accumulate error unless bias slope is calculated from an accumulator
  // bias slope may also need to be proportional to how much of the render we're doing here
  // multi.PrintInt32E(slope);
  for (size_t i = samples_rendered; i--; ) {
    current_output_value += slope;
    buffer->Overwrite(current_output_value >> 16);
  }

  if (samples_rendered == edge.samples) {
    edge.samples = 0; // Probably unncessary
    current_edge_ = (current_edge_ + 1) % kNumEdges;
    if (current_edge_ == 0) { // Begin next segment
      value_ = expo_->target;
      Trigger(static_cast<EnvelopeSegment>(segment_ + 1), false);
    } else {
      // jump to edge target?
    }

    render_samples_needed -= samples_rendered;
    if (render_samples_needed) {
      // NB: may cause yet another Trigger if next segment is short
      return RenderSamples(buffer, new_output_bias, render_samples_needed);
    }
  } else {
    // We'll continue rendering this segment next time
    value_ = current_output_value - new_output_bias; // CONSTRAIN complicates this
    edge.samples -= samples_rendered;
  }
}

template void Envelope::RenderSamples(
  stmlib::RingBuffer<int16_t, kAudioBlockSize>* buffer, int32_t new_output_bias, size_t render_samples_needed);
template void Envelope::RenderSamples(
  stmlib::RingBuffer<int16_t, kAudioBlockSize * 2>* buffer, int32_t new_output_bias, size_t render_samples_needed);

}  // namespace yarns
