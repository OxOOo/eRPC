#pragma once

#include <errno.h>
#include <malloc.h>
#include <numaif.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdexcept>
#include <vector>
#include <memory>
#include <stdint.h>

#include "common.h"
#include "transport.h"
#include "util/buffer.h"
#include "util/rand.h"

namespace erpc {

class STDAlloc {
 private:
  std::allocator<uint8_t> allocator;
 public:

  /**
   * @brief Construct the hugepage allocator
   * @throw runtime_error if construction fails
   */
  STDAlloc() {}
  ~STDAlloc() {}

  /**
   * @brief Allocate a Buffer.
   *
   * @param size The minimum size of the allocated Buffer. \p size need not
   * equal a class size.
   *
   * @return The allocated buffer. The buffer is invalid if we ran out of
   * memory.
   *
   * @throw runtime_error if \p size is too large for the allocator, or if
   * hugepage reservation failure is catastrophic
   */
  Buffer alloc(size_t size) {
    uint8_t* data = allocator.allocate(size);
    return Buffer(data, size);
  }

  /// Free a Buffer
  inline void free_buf(Buffer buffer) {
    allocator.deallocate(buffer.buf, buffer.size);
  }
};

}  // namespace erpc
