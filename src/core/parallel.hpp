// Minimal persistent thread pool.
#pragma once

#include <functional>

namespace bs {

// Splits [begin, end) into chunks and runs fn(lo, hi) on the pool. Blocks
// until done. Chunks must write disjoint data; under that rule results are
// identical to a serial run, which keeps the whole engine deterministic.
// Nested calls run serially on the calling thread.
void parallel_for(int begin, int end, const std::function<void(int, int)>& fn, int min_chunk = 1);

int parallel_threads();

}  // namespace bs
