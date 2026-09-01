#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "detection_dispatcher.h"

// 운영 dispatcher를 잠시 멈추고 동일 snapshot을 worker 수별로 재생하는 서버 권위
// 테스트 컨트롤러. UI는 이 객체의 JSON과 export 파일만 읽고 자체 집계를 하지 않는다.
class TestRunController {
 public:
  TestRunController(RawFrameStore* raw_store, DetectionDispatcher* dispatcher);
  ~TestRunController();

  bool Start(const std::string& request_body, int* status_code, std::string* response);
  std::string StatusJson() const;
  std::string CancelJson();
  bool IsActive() const;
  bool GetExportPath(const std::string& kind, std::string* path) const;

 private:
  struct RunConfig;
  struct RunState;

  bool ParseStartRequest(const std::string& request_body, const DetectionSettings& settings,
                         const std::vector<int>& active_channels, RunConfig* config,
                         std::string* error) const;
  void Run(RunConfig config);
  void WriteExportsLocked(RunState* state);
  bool IsCancelRequested() const;
  void OnDispatcherCompletion(const DispatcherCompletion& completion);

  RawFrameStore* raw_store_ = nullptr;
  DetectionDispatcher* dispatcher_ = nullptr;
  mutable std::mutex mutex_;
  std::thread thread_;
  std::unique_ptr<RunState> state_;
  bool cancel_requested_ = false;
  uint64_t next_run_number_ = 1;
  std::condition_variable completion_condition_;
  std::vector<DispatcherCompletion> completions_;
};
