#include "detection_dispatcher.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <exception>
#include <iomanip>
#include <sstream>

#include <opencv2/core.hpp>

namespace {

std::once_flag g_opencv_thread_policy_once;

int ElapsedUs(const std::chrono::steady_clock::time_point& begin,
              const std::chrono::steady_clock::time_point& end) {
  return static_cast<int>(std::max<int64_t>(0,
      std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count()));
}

}  // namespace

DetectionDispatcher::DetectionDispatcher(RawFrameStore* raw_store, SendFn send)
    : raw_store_(raw_store), send_(std::move(send)) {
  if (raw_store_ != nullptr) {
    raw_store_->SetNotify([this](int channel) { OnFrameReady(channel); });
  }
}

DetectionDispatcher::~DetectionDispatcher() {
  Stop();
  if (raw_store_ != nullptr) raw_store_->SetNotify(RawFrameStore::NotifyFn());
}

void DetectionDispatcher::Start(const DetectionSettings& settings) {
  Stop();
  if (raw_store_ == nullptr) return;
  // OpenCV 내부 병렬화가 worker 병렬화와 중첩되지 않도록 프로세스에서 한 번만
  // 1로 제한한다. worker 생성 전에 호출해야 첫 detector도 같은 정책을 따른다.
  std::call_once(g_opencv_thread_policy_once, []() { cv::setNumThreads(1); });

  std::vector<std::string> errors;
  DetectionSettings effective_settings = settings;
  const bool valid_settings = ValidateDetectionSettings(effective_settings, &errors);
  if (!valid_settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = settings;
    runtime_ = CollectRuntimeInfo(settings.detection_worker_count);
    runtime_.opencv_thread_count = cv::getNumThreads();
    runtime_.degraded = true;
    runtime_.degraded_reason = errors.empty() ? "invalid detection settings" : errors.front();
    active_channels_.clear();
    channel_configs_.clear();
    channel_status_.clear();
    in_flight_channels_.clear();
    last_channel_completion_.clear();
    cursor_ = 0;
    running_ = false;
    return;
  }
  const RuntimeInfo runtime = CollectRuntimeInfo(effective_settings.detection_worker_count);
  bool should_run = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = effective_settings;
    runtime_ = runtime;
    runtime_.opencv_thread_count = cv::getNumThreads();
    active_channels_.clear();
    channel_configs_.clear();
    channel_status_.clear();
    in_flight_channels_.clear();
    last_channel_completion_.clear();
    cursor_ = 0;
    for (const auto& channel : settings_.channels) {
      if (!channel.enabled) continue;
      active_channels_.push_back(channel.channel);
      channel_configs_[channel.channel] = channel;
      DispatcherChannelStatus status;
      status.channel = channel.channel;
      status.running = true;
      status.state = "waiting_frame";
      status.scale = channel.scale;
      channel_status_[channel.channel] = status;
    }
    started_at_ = std::chrono::steady_clock::now();
    running_ = !active_channels_.empty();
    for (auto& item : channel_status_) item.second.running = running_;
    should_run = running_;
  }
  if (!should_run) return;
  for (int channel : active_channels_) raw_store_->RequestFrame(channel);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    workers_.reserve(static_cast<size_t>(runtime_.effective_worker_count));
    for (int worker_id = 0; worker_id < runtime_.effective_worker_count; ++worker_id) {
      workers_.emplace_back(&DetectionDispatcher::WorkerLoop, this, worker_id);
    }
  }
}

void DetectionDispatcher::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
  }
  condition_.notify_all();
  std::vector<std::thread> workers;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    workers.swap(workers_);
    in_flight_channels_.clear();
    for (auto& item : channel_status_) item.second.running = false;
  }
  for (std::thread& worker : workers) {
    if (worker.joinable()) worker.join();
  }
}

bool DetectionDispatcher::IsChannelActive(int channel) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return channel_configs_.find(channel) != channel_configs_.end();
}

DetectionSettings DetectionDispatcher::GetSettings() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return settings_;
}

std::vector<int> DetectionDispatcher::ActiveChannels() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_channels_;
}

void DetectionDispatcher::SetCompletionObserver(CompletionObserver observer) {
  std::lock_guard<std::mutex> lock(mutex_);
  completion_observer_ = std::move(observer);
}

DispatcherStatus DetectionDispatcher::GetStatus() const {
  DispatcherStatus result;
  std::lock_guard<std::mutex> lock(mutex_);
  result.running = running_;
  if (running_) {
    result.uptime_ms = static_cast<uint64_t>(std::max<int64_t>(0,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at_).count()));
  }
  result.runtime = runtime_;
  result.active_workers = static_cast<int>(workers_.size());
  result.channels = channel_status_;
  result.in_flight_channels.assign(in_flight_channels_.begin(), in_flight_channels_.end());
  return result;
}

void DetectionDispatcher::OnFrameReady(int /*channel*/) {
  condition_.notify_all();
}

bool DetectionDispatcher::TakeTask(Task* task) {
  if (task == nullptr || raw_store_ == nullptr) return false;
  std::unique_lock<std::mutex> lock(mutex_);
  while (running_) {
    if (active_channels_.empty()) {
      condition_.wait(lock, [this]() { return !running_ || !active_channels_.empty(); });
      continue;
    }
    const auto dispatch_begin = std::chrono::steady_clock::now();
    for (size_t offset = 0; offset < active_channels_.size(); ++offset) {
      const size_t index = (cursor_ + offset) % active_channels_.size();
      const int channel = active_channels_[index];
      if (in_flight_channels_.find(channel) != in_flight_channels_.end()) continue;
      RawFrameStore::FrameSnapshot snapshot;
      const auto frame_get_begin = std::chrono::steady_clock::now();
      if (!raw_store_->AcquirePending(channel, &snapshot)) continue;
      const auto frame_get_end = std::chrono::steady_clock::now();
      cursor_ = (index + 1) % active_channels_.size();
      in_flight_channels_.insert(channel);
      DispatcherChannelStatus& status = channel_status_[channel];
      status.state = "processing";
      task->channel = channel;
      task->snapshot = snapshot;
      task->frame_get_us = ElapsedUs(frame_get_begin, frame_get_end);
      task->dispatch_scan_us = ElapsedUs(dispatch_begin, frame_get_begin);
      task->queue_wait_us = ElapsedUs(snapshot.snapshot_ready_at, frame_get_begin);
      return true;
    }
    condition_.wait(lock);
  }
  return false;
}

cv::Ptr<ArucoDetector> DetectionDispatcher::GetDetector(WorkerContext* context) {
  if (context->detector.empty()) {
    context->detector = ArucoDetector::Create(StringToDict(settings_.dictionary_name));
  }
  return context->detector;
}

void DetectionDispatcher::WorkerLoop(int /*worker_id*/) {
  WorkerContext context;
  while (true) {
    Task task;
    if (!TakeTask(&task)) break;

    const auto setup_begin = std::chrono::steady_clock::now();
    ChannelConfig channel_config;
    cv::Ptr<ArucoDetector> detector;
    PipelineOutput output;
    try {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        auto channel_it = channel_configs_.find(task.channel);
        if (channel_it != channel_configs_.end()) {
          channel_config = channel_it->second;
        }
      }
      detector = GetDetector(&context);
    } catch (const std::exception& exception) {
      output.error = exception.what();
    } catch (...) {
      output.error = "unknown worker setup error";
    }
    const int worker_setup_us = ElapsedUs(setup_begin, std::chrono::steady_clock::now());

    if (output.error.empty()) {
      if (detector.empty()) {
        output.error = "failed to create detector";
      } else {
        try {
          output = DetectionPipeline::Run(
              task.snapshot, *detector, channel_config.scale,
              [this, task](const DetectionResult& result) {
                if (send_) send_(task.channel, result);
              });
        } catch (const std::exception& exception) {
          output.error = exception.what();
        } catch (...) {
          output.error = "unknown detection pipeline error";
        }
      }
    }
    output.timing.input_copy_us = task.snapshot.snapshot_copy_us;
    output.timing.queue_wait_us = task.queue_wait_us;
    output.timing.dispatch_scan_us = task.dispatch_scan_us;
    output.timing.frame_get_us = task.frame_get_us;
    output.timing.worker_setup_us = worker_setup_us;
    output.timing.end_to_end_us = output.timing.input_copy_us +
                                  output.timing.queue_wait_us +
                                  output.timing.frame_get_us +
                                  output.timing.worker_setup_us +
                                  output.timing.processing_total_us;
    UpdateStatus(task.channel, task, output);
    raw_store_->Complete(task.channel, task.snapshot.generation);
    CompletionObserver observer;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      observer = completion_observer_;
    }
    if (observer) {
      DispatcherCompletion completion;
      completion.channel = task.channel;
      completion.config = channel_config;
      completion.snapshot = task.snapshot;
      completion.output = output;
      observer(completion);
    }
    condition_.notify_all();

  }
}

void DetectionDispatcher::UpdateStatus(int channel, const Task& task,
                                       const PipelineOutput& output) {
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(mutex_);
  in_flight_channels_.erase(channel);
  DispatcherChannelStatus& status = channel_status_[channel];
  status.running = running_;
  status.state = output.error.empty() ? "waiting_frame" : "error";
  status.frame_generation = task.snapshot.generation;
  status.marker_count = static_cast<int>(output.result.ids.size());
  status.marker_ids = output.result.ids;
  status.rejected_count = output.result.rejected_count;
  status.input_copy_us = output.timing.input_copy_us;
  status.queue_wait_us = output.timing.queue_wait_us;
  status.dispatch_scan_us = output.timing.dispatch_scan_us;
  status.frame_get_us = output.timing.frame_get_us;
  status.worker_setup_us = output.timing.worker_setup_us;
  status.resize_us = output.timing.resize_us;
  status.preprocess_us = output.timing.preprocess_us;
  status.detect_us = output.timing.detect_us;
  status.coordinate_restore_us = output.timing.coordinate_restore_us;
  status.send_us = output.timing.send_us;
  status.processing_total_us = output.timing.processing_total_us;
  status.end_to_end_us = output.timing.end_to_end_us;
  status.thread_cpu_us = output.timing.thread_cpu_us;
  status.completed_count++;
  if (!output.error.empty()) status.last_error = output.error;
  else status.last_error.clear();
  status.last_detect = NowIso8601();
  const auto previous = last_channel_completion_.find(channel);
  if (previous != last_channel_completion_.end()) {
    status.channel_cycle_us = ElapsedUs(previous->second, now);
  }
  last_channel_completion_[channel] = now;
}

std::string DetectionDispatcher::NowIso8601() {
  const std::time_t now = std::time(nullptr);
  std::tm utc;
#if defined(_WIN32)
  gmtime_s(&utc, &now);
#else
  gmtime_r(&now, &utc);
#endif
  std::ostringstream stream;
  stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}
