#if defined(__linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#endif

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include "runtime_info.h"

#include <algorithm>
#include <string>

#include <opencv2/core.hpp>

#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#endif

RuntimeInfo CollectRuntimeInfo(int configured_worker_count) {
  RuntimeInfo info;
  info.configured_worker_count = configured_worker_count;
#if defined(__linux__)
  const long online = sysconf(_SC_NPROCESSORS_ONLN);
  info.online_cpu_count = online > 0 ? static_cast<int>(online) : 1;
  cpu_set_t affinity;
  CPU_ZERO(&affinity);
  if (sched_getaffinity(0, sizeof(affinity), &affinity) == 0) {
    const int allowed = CPU_COUNT(&affinity);
    info.allowed_cpu_count = allowed > 0 ? allowed : 1;
  } else {
    info.allowed_cpu_count = info.online_cpu_count;
  }
#else
  info.online_cpu_count = 1;
  info.allowed_cpu_count = 1;
#endif
  info.effective_worker_count = std::max(1, std::min(configured_worker_count,
                                                       info.allowed_cpu_count));
  info.degraded = info.effective_worker_count != configured_worker_count;
  if (info.degraded) {
    info.degraded_reason = "configured worker count exceeds allowed CPU count";
  }
  info.opencv_thread_count = cv::getNumThreads();
  return info;
}
