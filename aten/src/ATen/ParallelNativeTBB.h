#pragma once
#include <ATen/ATen.h>

#include <cstddef>
#include <exception>

#include "tbb/tbb.h"

#define INTRA_OP_PARALLEL

namespace at {

namespace internal {
CAFFE2_API tbb::task_arena& _get_arena();
}

template <class F>
inline void parallel_for(
    const int64_t begin,
    const int64_t end,
    const int64_t grain_size,
    const F& f) {
  TORCH_CHECK(grain_size >= 0);
  if (begin >= end) {
    return;
  }

  if ((end - begin) < grain_size || get_num_threads() == 1) {
    f(begin, end);
  } else {
    std::atomic_flag err_flag = ATOMIC_FLAG_INIT;
    std::exception_ptr eptr;
    tbb::task_group tg;
    internal::_get_arena().execute(
        [&tg, &eptr, &err_flag, begin, end, grain_size, f]() {
      tg.run([&eptr, &err_flag, begin, end, grain_size, f]() {
        tbb::parallel_for(tbb::blocked_range<int64_t>(begin, end, grain_size),
          [&eptr, &err_flag, f](const tbb::blocked_range<int64_t>& r) {
            try {
              f(r.begin(), r.end());
            } catch (...) {
              if (!err_flag.test_and_set()) {
                eptr = std::current_exception();
              }
            }
          });
      });
    });
    tg.wait();
    if (eptr) {
      std::rethrow_exception(eptr);
    }
  }
}

template <class scalar_t, class F, class SF>
inline scalar_t parallel_reduce(
    const int64_t begin,
    const int64_t end,
    const int64_t grain_size,
    const scalar_t ident,
    const F& f,
    const SF& sf) {
  TORCH_CHECK(grain_size >= 0);
  if (begin >= end) {
    return ident;
  }

  if ((end - begin) < grain_size || get_num_threads() == 1) {
    return f(begin, end, ident);
  } else {
    scalar_t result;
    std::atomic_flag err_flag = ATOMIC_FLAG_INIT;
    std::exception_ptr eptr;
    tbb::task_group tg;
    internal::_get_arena().execute(
        [&tg, &result, &eptr, &err_flag, begin, end, grain_size, ident, f, sf]() {
      tg.run([&result, &eptr, &err_flag, begin, end, grain_size, ident, f, sf]() {
        result = tbb::parallel_reduce(
          tbb::blocked_range<int64_t>(begin, end, grain_size), ident,
          [&eptr, &err_flag, f, ident]
              (const tbb::blocked_range<int64_t>& r, scalar_t ident) {
            try {
              return f(r.begin(), r.end(), ident);
            } catch (...) {
              if (!err_flag.test_and_set()) {
                eptr = std::current_exception();
              }
              return ident;
            }
          },
          sf
        );
      });
    });
    tg.wait();
    if (eptr) {
      std::rethrow_exception(eptr);
    }
    return result;
  }
}

} // namespace at