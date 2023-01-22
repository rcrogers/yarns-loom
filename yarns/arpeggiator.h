// Copyright 2013 Emilie Gillet.
// Copyright 2019 Chris Rogers.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
// Author: Chris Rogers (teukros@gmail.com)
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
// Arpeggiator.

#ifndef YARNS_ARPEGGIATOR_H_
#define YARNS_ARPEGGIATOR_H_

#include "stmlib/stmlib.h"

#include "yarns/sequencer_step.h"

namespace yarns {

using namespace stmlib;

class Part;
class HeldKeys;
class SequencerArpeggiatorResult;

struct Arpeggiator {
  int8_t key_index;
  int8_t octave;
  int8_t key_increment;

  void Reset();

  const SequencerArpeggiatorResult BuildNextResult(
    const Part& part,
    const HeldKeys& arp_keys,
    uint32_t step_counter,
    const SequencerStep seq_step
  ) const;

};

}

#endif // YARNS_ARPEGGIATOR_H_