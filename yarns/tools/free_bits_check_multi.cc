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
// Proves PackedMulti::kFreeBits names ALL of the multi's leftover bits, not just
// some.  Claiming too many is caught by the page assert, but claiming too few
// is invisible: the bits exist, no count mentions them, and the build quietly
// under-reports what is free.
//
// One more bit than the multi really has must not fit.  Since the true blob
// fills the page exactly, a multi that grows makes the blob overflow -- and a
// multi with room to spare leaves it exactly full, which is the failure.
//
// Compiled by the `syx` rule, never linked.

#define YARNS_FREE_BITS_CHECK
#define PACKED_MULTI_EXTRA_FREE_BITS 1

#include "yarns/storage_manager.h"

namespace yarns {

STATIC_ASSERT(kPackedSize > kPackedMaxSize, multi_free_bits_understated);

}  // namespace yarns
