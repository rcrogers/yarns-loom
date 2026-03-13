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
// Storage manager.

#include "yarns/storage_manager.h"

#include "yarns/midi_handler.h"
#include "yarns/multi.h"

namespace yarns {

// Assert packed size satisfies flash constraints
STATIC_ASSERT(kPackedSize % 4 == 0, flash_aligns_packed);
STATIC_ASSERT(kPackedSize <= FlashStorage::MAX_DATA_SIZE, flash_fits_packed);

// Informational
// char (*__debug_packed)[kPackedSize] = 1;
STATIC_ASSERT(kPackedSize == 1016, i_just_want_to_know_if_this_changes);

STATIC_ASSERT(kStreamBufferSize >= kPackedSize, buffer_fits_packed);
STATIC_ASSERT(kStreamBufferSize >= Multi::kTaggedPayloadSize, buffer_fits_tagged);

void StorageManager::SaveMulti(uint8_t slot) {
  stream_buffer_.Rewind();
  multi.SerializePacked(&stream_buffer_);
  storage_.Save(stream_buffer_.bytes(), stream_buffer_.position(), 1 + slot);
}

bool StorageManager::LoadMulti(uint8_t slot) {
  // Dummy serialization of the multi to know its size.
  stream_buffer_.Rewind();
  multi.SerializePacked(&stream_buffer_);
  uint32_t expected_size = stream_buffer_.position();
  
  if (!storage_.Load(stream_buffer_.mutable_bytes(), expected_size, 1 + slot)) {
    return false;
  } else {
    DeserializeMultiPacked();
    return true;
  }
}

void StorageManager::SaveCalibration() {
  stream_buffer_.Rewind();
  multi.SerializeCalibration(&stream_buffer_);
  storage_.Save(stream_buffer_.bytes(), stream_buffer_.position(), 0);
}

bool StorageManager::LoadCalibration() {
  stream_buffer_.Rewind();
  multi.SerializeCalibration(&stream_buffer_);
  uint32_t expected_size = stream_buffer_.position();
  
  if (!storage_.Load(stream_buffer_.mutable_bytes(), expected_size, 0)) {
    return false;
  } else {
    stream_buffer_.Rewind();
    multi.DeserializeCalibration(&stream_buffer_);
    return true;
  }
}

void StorageManager::SysExSendMultiPacked() {
  stream_buffer_.Rewind();
  multi.SerializePacked(&stream_buffer_);
  midi_handler.SysExSendPackets(
      stream_buffer_.bytes(),
      stream_buffer_.position());
}

void StorageManager::SysExSendMultiTagged() {
  stream_buffer_.Rewind();
  multi.SerializeTagged(&stream_buffer_);
  midi_handler.SysExSendPackets(
      stream_buffer_.bytes(),
      stream_buffer_.position(),
      SYSEX_COMMAND_DUMP_PACKET_TAGGED);
}

bool StorageManager::DeserializeMultiPacked() {
  stream_buffer_.Rewind();
  multi.DeserializePacked(&stream_buffer_);
  return true;  // Packed format has no structural validation
}

bool StorageManager::DeserializeMultiTagged() {
  stream_buffer_.Rewind();
  return multi.DeserializeTagged(&stream_buffer_);
}

/* extern */
StorageManager storage_manager;

}  // namespace yarns