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

  template<size_t BUFFER_SIZE>
  void RenderSamples(stmlib::RingBuffer<int16_t, BUFFER_SIZE>* buffer, int32_t new_bias);

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
