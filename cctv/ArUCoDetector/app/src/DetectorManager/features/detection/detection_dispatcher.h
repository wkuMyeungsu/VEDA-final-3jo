#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "detection_pipeline.h"
#include "detection_settings.h"
#include "runtime_info.h"

struct DispatcherChannelStatus {
  int channel = 0;
  bool running = false;
  std::string state = "idle";
  double scale = 1.0;
  uint64_t frame_generation = 0;
  int marker_count = 0;
  std::vector<int> marker_ids;
  int rejected_count = 0;
  int input_copy_us = 0;
  int queue_wait_us = 0;
  int dispatch_scan_us = 0;
  int frame_get_us = 0;
  int worker_setup_us = 0;
  int resize_us = 0;
  int preprocess_us = 0;
  int detect_us = 0;
  int coordinate_restore_us = 0;
  int send_us = 0;
  int processing_total_us = 0;
  int end_to_end_us = 0;
  int thread_cpu_us = 0;
  int channel_cycle_us = 0;
  uint64_t completed_count = 0;
  uint64_t skipped_count = 0;
  std::string last_detect;
  std::string last_error;
};

struct DispatcherStatus {
  bool running = false;
  uint64_t uptime_ms = 0;
  RuntimeInfo runtime;
  int active_workers = 0;
  std::vector<int> in_flight_channels;
  std::map<int, DispatcherChannelStatus> channels;
};

// 운영 dispatcher가 실제로 처리 완료한 작업의 불변 복사본이다. 테스트는 이
// 관찰 지점만 사용하므로 운영과 별도의 detect worker를 만들지 않는다.
struct DispatcherCompletion {
  int channel = 0;
  ChannelConfig config;
  RawFrameStore::FrameSnapshot snapshot;
  PipelineOutput output;
};

class DetectionDispatcher {
 public:
  using SendFn = std::function<void(int, const DetectionResult&)>;
  using CompletionObserver = std::function<void(const DispatcherCompletion&)>;

  DetectionDispatcher(RawFrameStore* raw_store, SendFn send);
  ~DetectionDispatcher();

  DetectionDispatcher(const DetectionDispatcher&) = delete;
  DetectionDispatcher& operator=(const DetectionDispatcher&) = delete;

  void Start(const DetectionSettings& settings);
  void Stop();
  bool IsChannelActive(int channel) const;
  DispatcherStatus GetStatus() const;
  DetectionSettings GetSettings() const;
  std::vector<int> ActiveChannels() const;
  void SetCompletionObserver(CompletionObserver observer);

 private:
  struct Task {
    int channel = 0;
    RawFrameStore::FrameSnapshot snapshot;
    int frame_get_us = 0;
    int dispatch_scan_us = 0;
    int queue_wait_us = 0;
  };

  struct WorkerContext {
    cv::Ptr<ArucoDetector> detector;
  };

  void WorkerLoop(int worker_id);
  bool TakeTask(Task* task);
  void OnFrameReady(int channel);
  cv::Ptr<ArucoDetector> GetDetector(WorkerContext* context);
  void UpdateStatus(int channel, const Task& task, const PipelineOutput& output);
  static std::string NowIso8601();

  RawFrameStore* raw_store_ = nullptr;
  SendFn send_;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  bool running_ = false;
  DetectionSettings settings_;
  RuntimeInfo runtime_;
  std::vector<int> active_channels_;
  std::map<int, ChannelConfig> channel_configs_;
  std::map<int, DispatcherChannelStatus> channel_status_;
  std::set<int> in_flight_channels_;
  std::vector<std::thread> workers_;
  size_t cursor_ = 0;
  std::chrono::steady_clock::time_point started_at_;
  std::map<int, std::chrono::steady_clock::time_point> last_channel_completion_;
  CompletionObserver completion_observer_;
};
