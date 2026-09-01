#pragma once

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include <string>

struct RuntimeInfo {
  int online_cpu_count = 1;
  int allowed_cpu_count = 1;
  int configured_worker_count = 2;
  int effective_worker_count = 1;
  int opencv_thread_count = 1;
  bool degraded = false;
  std::string degraded_reason;
};

// 앱 사용 가능 CPU만 확인하고 affinity를 변경하지 않는다. 실제 worker 수가
// 허용 CPU보다 크면 설정값은 그대로 보존한 채 effective만 낮춘다.
RuntimeInfo CollectRuntimeInfo(int configured_worker_count);
