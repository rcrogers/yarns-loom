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

#ifndef YARNS_ENVELOPE_H_
#define YARNS_ENVELOPE_H_

#include "stmlib/utils/ring_buffer.h"
#include "stmlib/dsp/dsp.h"

#include "yarns/resources.h"

// #include "yarns/multi.h" // TODO

namespace yarns {

using namespace stmlib;

const uint8_t kAudioBlockSizeBits = 6;
const size_t kAudioBlockSize = 1 << kAudioBlockSizeBits;

const uint8_t kEdgeBits = 3;
const uint8_t kNumEdges = 1 << kEdgeBits;
STATIC_ASSERT(kNumEdges == LUT_EXPO_SLOPE_SHIFT_SIZE, kNumEdges);

enum EnvelopeStage {
  ENV_STAGE_ATTACK,   // manual start, auto/manual end
  ENV_STAGE_DECAY,    // auto start, auto/manual end
  ENV_STAGE_SUSTAIN,  // no motion
  ENV_STAGE_RELEASE,  // manual start, auto end
  ENV_STAGE_DEAD,     // no motion
  ENV_NUM_STAGES,
};

struct ADSR {
  uint16_t peak, sustain; // Platonic, unscaled targets
  size_t attack, decay, release;
};

struct ExpoCurve {
  size_t nominal_samples; // X length
  int32_t target, nominal_offset; // Y values
};

struct Edge {
  size_t samples;
  int32_t slope;
};

struct InlineLocalSampleBuffer {
  int16_t buffer[kAudioBlockSize];
};

class Envelope {
 public:
  Envelope() { }
  ~Envelope() { }

  void Init(int32_t value);
  // Compute the max damp-ability of the envelope for a given tremolo strength
  int16_t tremolo(uint16_t strength) const;
  void NoteOff();
  void NoteOn(
    ADSR& adsr,
    int32_t min_target, int32_t max_target // Actual bounds, 16-bit signed
  );
  void Trigger(EnvelopeStage stage); // Populates expo slope table for the new stage
  int32_t compute_edge_slope(int32_t linear_slope, uint8_t edge, uint8_t max_shift) const;

  /* Render TODO

  Try copying expo slope to local

  Separate inline buffer of length 4-8?
  Write to output buffer 4 samples at a time?

  Try outputting slopes instead, then computing a running total
  https://claude.ai/chat/127cae75-04b9-4d95-87ac-d01114bd7cf3
  Complication: shifting slopes to 16-bit before summing will cause error
  Running total must be 32-bit, slope vector must be 32-bit, output buffer can be 16-bit

  Options for vector accumulator:
  1. Offset slope vector
    - Render buffer of envelope slopes
      - Have to handle corners
    - Offset envelope slope buffer by interpolator slope
    - Compute prefix sum of slope buffer
  2. Apply slope/value bias inline, like an idiot
    - Optionally MAKE_BIASED_EXPO_SLOPES
  3. Sum 2 q15 vectors
    - Render buffer of interpolator values (with prefix sum?)
    - Sum interpolator value buffer with envelope buffer
  4. In-place addition on single buffer
    - Render buffer of interpolator values (with prefix sum?)
    - In render loop, SatAdd with envelope value (into same buffer)
  */

  #define RENDER_LOOP(samples_exp, slope_exp) \
    const int32_t slope = (slope_exp); \
    size_t samples = (samples_exp); \
    do { \
      value += slope; \
      bias += bias_slope; \
      int32_t biased_value = (value >> 16) + (bias >> 16); \
      res.buffer[local_buffer_index++] = Clip16(biased_value); \
    } while (--samples);

  InlineLocalSampleBuffer RenderSamples(int32_t new_bias) __attribute__((always_inline)) {
    size_t samples_needed = kAudioBlockSize; // Even if double buffering
    InlineLocalSampleBuffer res;
    size_t local_buffer_index = 0;

    int32_t value = value_;
    int32_t bias = bias_;
    const int32_t bias_slope = ((new_bias >> 1) - (bias >> 1)) >> (kAudioBlockSizeBits - 1);

    while (true) {
      if (expo_) {
        const Edge edge = edges_[current_edge_];
        const bool finish_edge = samples_needed >= edge.samples;
        const size_t edge_samples_to_render = finish_edge ? edge.samples : samples_needed;

        RENDER_LOOP(edge_samples_to_render, edge.slope);

        if (finish_edge) {
          // Advance edge/stage as needed
          samples_needed -= edge.samples;
          current_edge_++;
          // multi.PrintDebugByte(0x0C + (current_edge_ << 4));
          if (current_edge_ == kNumEdges) { // Stage is done
            // TODO this may glitch, but hard to reconstruct value_ from biased_value because we are somewhere between old and new bias
            value = value_ = expo_->target;
            Trigger(static_cast<EnvelopeStage>(stage_ + 1));
            // what happens to biased_value_31 if we go to SUSTAIN/DEAD?  prob fucked
          }
          if (!samples_needed) break;
        } else {
          // Save edge state
          edges_[current_edge_].samples -= edge_samples_to_render;
          break;
        }
      } else {
        RENDER_LOOP(samples_needed, 0);
        break;
      }
    }

    value_ = value;
    bias_ = bias;

    return res;
  }

 private:
  ExpoCurve attack_, decay_, release_;
  
  // Current stage.
  EnvelopeStage stage_;
  ExpoCurve* expo_;

  // State of the current motion stage
  Edge edges_[kNumEdges];
  uint8_t current_edge_;

  int32_t value_;
  int32_t bias_;

  DISALLOW_COPY_AND_ASSIGN(Envelope);
};

}  // namespace yarns

#endif  // YARNS_ENVELOPE_H_
