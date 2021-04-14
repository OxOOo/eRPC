#pragma once

#include "common.h"

namespace erpc {

/// A class to hold a fixed-size buffer. The size of the buffer is read-only
/// after the Buffer is created.
class Buffer {
 public:
  Buffer(uint8_t *buf, uint64_t size) : buf(buf), size(size) {}

  Buffer() {}

  /// Since \p Buffer does not allocate its own \p buf, do nothing here.
  ~Buffer() {}

  /// Return a string representation of this Buffer (excluding lkey)
  std::string to_string() const {
    std::ostringstream ret;
    ret << "[buf " << static_cast<void *>(buf) << "]";
    return ret.str();
  }

  /// The backing memory of this Buffer. The Buffer is invalid if this is null.
  uint8_t *buf;
  uint64_t size;
};

}  // namespace erpc
