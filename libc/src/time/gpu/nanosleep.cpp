//===-- GPU implementation of the nanosleep function ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/time/nanosleep.h"

#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/__support/time/gpu/time_utils.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, nanosleep,
                   (const struct timespec *req, struct timespec *rem)) {
  if (!GPU_CLOCKS_PER_SEC || !req)
    return -1;

  uint64_t nsecs = req->tv_nsec + req->tv_sec * TICKS_PER_SEC;
  uint64_t tick_rate = TICKS_PER_SEC / GPU_CLOCKS_PER_SEC;

  uint64_t start = gpu::fixed_frequency_clock();
#if defined(LIBC_TARGET_ARCH_IS_NVPTX)
  uint64_t end = start + (nsecs + tick_rate - 1) / tick_rate;
  uint64_t cur = gpu::fixed_frequency_clock();
  // The NVPTX architecture supports sleeping and guaruntees the actual time
  // slept will be somewhere between zero and twice the requested amount. Here
  // we will sleep again if we undershot the time.
  while (cur < end) {
    if (__nvvm_reflect("__CUDA_ARCH") >= 700)
      LIBC_INLINE_ASM("nanosleep.u32 %0;" ::"r"(nsecs));
    cur = gpu::fixed_frequency_clock();
    nsecs -= nsecs > cur - start ? cur - start : 0;
  }
#elif defined(LIBC_TARGET_ARCH_IS_AMDGPU)
  uint64_t end = start + (nsecs + tick_rate - 1) / tick_rate;
  uint64_t cur = gpu::fixed_frequency_clock();
  // The AMDGPU architecture does not provide a sleep implementation with a
  // known delay so we simply repeatedly sleep with a large value of ~960 clock
  // cycles and check until we've passed the time using the known frequency.
  __builtin_amdgcn_s_sleep(2);
  while (cur < end) {
    __builtin_amdgcn_s_sleep(15);
    cur = gpu::fixed_frequency_clock();
  }
#else
  // Sleeping is not supported.
  if (rem) {
    rem->tv_sec = req->tv_sec;
    rem->tv_nsec = req->tv_nsec;
  }
  return -1;
#endif
  uint64_t stop = gpu::fixed_frequency_clock();

  // Check to make sure we slept for at least the desired duration and set the
  // remaining time if not.
  uint64_t elapsed = (stop - start) * tick_rate;
  if (elapsed < nsecs) {
    if (rem) {
      rem->tv_sec = (nsecs - elapsed) / TICKS_PER_SEC;
      rem->tv_nsec = (nsecs - elapsed) % TICKS_PER_SEC;
    }
    return -1;
  }

  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
