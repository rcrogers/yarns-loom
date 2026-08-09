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
// Responsible for flash memory storage.

#ifndef YARNS_STORAGE_MANAGER_H_
#define YARNS_STORAGE_MANAGER_H_

#include <algorithm>

#include "stmlib/stmlib.h"

#include "stmlib/utils/stream_buffer.h"
#include "stmlib/system/storage.h"

#include "yarns/multi.h"

namespace yarns {

typedef stmlib::Storage<0x8020000, 9> FlashStorage;
const uint16_t kPackedSize = sizeof(PackedMulti);

// WriteBlock programs whole words and drops any remainder, so MAX_DATA_SIZE
// itself is unreachable.
const uint8_t kFlashWordBytes = 4;
const uint16_t kPackedMaxSize =
    FlashStorage::MAX_DATA_SIZE / kFlashWordBytes * kFlashWordBytes;

// Suspended for the free-bits checks, which deliberately overflow the page.
#ifndef YARNS_FREE_BITS_CHECK

// The blob fills the page exactly, so its size never moves and adding a setting
// never invalidates a saved patch.  When this fires, PackedMulti::
// kUnassignedBytes is the knob; the page is only exhausted once that hits zero.
STATIC_ASSERT(kPackedSize == kPackedMaxSize, resize_unassigned_bytes_to_fill_page);
#endif

const uint8_t kBitsPerByte = 8;

// Every count below is in PAGE bits -- storage as the flash holds it, not width
// as a setting declares it.  The two differ by scope: a part setting of width W
// takes W bits from kFreeBitsPerPartBitfield but 4W bits from the page, since
// every part carries its own copy.
//
// Bitfield bits are stuck where they are -- a part's cannot serve the multi,
// nor one part another.  Fungible bits are inside no struct yet, so they can
// still go to either scope: 8 bits per byte given to the multi, or per byte
// given to EACH part, which costs kNumParts bytes.
const uint16_t kFreeBitsPerPartBitfield = PackedPart::kFreeBits;
const uint16_t kFreeBitsMultiBitfield = PackedMulti::kFreeBits;
const uint16_t kFungibleFreeBits = PackedMulti::kUnassignedBytes * kBitsPerByte;
const uint16_t kTotalFreeBits = kFreeBitsPerPartBitfield * kNumParts +
    kFreeBitsMultiBitfield + kFungibleFreeBits;

// Keeps fungible bits fungible.  A whole free byte parked inside a struct is
// stranded there, so take bytes out of unassigned only to spend them.
#ifndef YARNS_FREE_BITS_CHECK
STATIC_ASSERT(PackedMulti::kFreeBits < kBitsPerByte, multi_free_bits_exceed_byte);
STATIC_ASSERT(PackedPart::kFreeBits < kBitsPerByte, part_free_bits_exceed_byte);
#endif

// Must fit both packed and tagged payloads.
const uint16_t kStreamBufferSize =
    Multi::kTaggedPayloadSize > kPackedSize
    ? Multi::kTaggedPayloadSize
    : kPackedSize;

class StorageManager {
 public:
  StorageManager() { }
  ~StorageManager() { }
  
  void SaveMulti(uint8_t slot);
  bool LoadMulti(uint8_t slot);
  void SaveCalibration();
  bool LoadCalibration();
  void SysExSendMultiPacked();
  void SysExSendMultiTagged();

  void AppendData(const uint8_t* data, size_t size, bool rewind) {
    if (rewind) {
      stream_buffer_.Rewind();
    }
    stream_buffer_.Write(data, size);
  }
  
  bool DeserializeMultiPacked();
  bool DeserializeMultiTagged();

 private:
  stmlib::StreamBuffer<kStreamBufferSize> stream_buffer_;
  FlashStorage storage_;
  
  DISALLOW_COPY_AND_ASSIGN(StorageManager);
};

extern StorageManager storage_manager;

}  // namespace yarns

#endif // YARNS_STORAGE_MANAGER_H_
