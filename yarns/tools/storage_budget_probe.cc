// Copyright 2026 Chris Rogers.
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
// Preset-storage budget probe.  Nothing keeps a compile-time constant anywhere
// a build can read it, so each array below carries one out as its symbol size,
// for `nm --print-size`.  The +1 keeps the array legal at zero; the makefile
// takes it back off.
//
// Compiled by the `syx` rule with the firmware's own flags, and outside the
// source packages so the build's wildcard never links it.

#include "yarns/storage_manager.h"

using namespace yarns;

extern "C" {

// The numeric prefix sets report order, since nm sorts alphabetically.

char probe_1_usable_bits_per_part[kUsableBitsPerPart + 1];
char probe_2_usable_bits_multi[kUsableBitsMulti + 1];
char probe_3_fungible_free_bits[kFungibleFreeBits + 1];
char probe_4_total_free_bits[kTotalFreeBits + 1];

}  // extern "C"
