#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include <opencv2/core.hpp>

#include "aruco_detector.h"

// 테스트가 운영 callback을 건드리지 않고도 같은 파이프라인을 반복할 수 있게
// 합성 입력을 저장소 자체에서 제공한다.
enum class TestFrameMode {
  kCamera = 0,
  kWhite = 1,
  kBlack = 2,
  kMarker = 3
};

class RawFrameStore {
 public:
  using Clock = std::chrono::steady_clock;

  struct FrameSnapshot {
    std::shared_ptr<const cv::Mat> image;
    uint64_t generation = 0;
    Clock::time_point source_received_at = Clock::now();
    Clock::time_point snapshot_ready_at = Clock::now();
    int snapshot_copy_us = 0;

    bool empty() const { return !image || image->empty(); }
    explicit operator bool() const { return !empty(); }
    int width() const { return empty() ? 0 : image->cols; }
    int height() const { return empty() ? 0 : image->rows; }
    size_t step() const { return empty() ? 0 : image->step; }
  };

  struct ChannelStats {
    uint64_t callback_count = 0;
    uint64_t clone_count = 0;
    uint64_t skipped_count = 0;
    uint64_t consumed_count = 0;
    uint64_t completed_count = 0;
    bool pending = false;
    bool in_flight = false;
    uint64_t pending_generation = 0;
    uint64_t in_flight_generation = 0;
    uint64_t last_generation = 0;
  };

  using NotifyFn = std::function<void(int)>;

  RawFrameStore() = default;
  RawFrameStore(const RawFrameStore&) = delete;
  RawFrameStore& operator=(const RawFrameStore&) = delete;

  // 대기 슬롯은 최신 프레임으로만 갱신한다. 처리량이 입력보다 낮아도 오래된
  // snapshot을 검출하지 않게 하고, in-flight 버퍼는 절대 덮어쓰지 않는다.
  void Put(int channel, const cv::Mat& gray) {
    PutFrame(channel, gray);
  }

  // 합성 입력도 카메라 입력과 똑같이 pending/in-flight 슬롯과 dispatcher notify를
  // 거친다. marker/white/black 테스트가 별도 검출 루프를 우회하지 않게 한다.
  bool PutSynthetic(int channel) {
    const FrameSnapshot synthetic = MakeSyntheticSnapshot(GetFrameMode());
    return !synthetic.empty() && PutFrame(channel, *synthetic.image);
  }

 private:
  bool PutFrame(int channel, const cv::Mat& gray) {
    if (channel < 1 || channel > 4 || gray.empty()) return false;
    ChannelSlot& slot = slots_[static_cast<size_t>(channel)];
    bool clone_requested = false;
    {
      std::lock_guard<std::mutex> lock(slot.mutex);
      ++slot.stats.callback_count;
      const Clock::time_point now = Clock::now();
      const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - slot.last_clone_at).count();
      // 5 FPS면 500ms급 검출보다 충분히 최신이고, callback마다 원본을 복사하지 않는다.
      if (!slot.copying && (slot.requested || slot.pending) &&
          (slot.last_clone_at == Clock::time_point::min() || elapsed_ms >= 200)) {
        slot.copying = true;
        clone_requested = true;
        slot.last_clone_at = now;
      } else {
        ++slot.stats.skipped_count;
      }
    }
    if (!clone_requested) return false;

    std::shared_ptr<cv::Mat> target_buf;
    uint64_t generation = 0;
    const Clock::time_point source_received_at = Clock::now();
    {
      std::lock_guard<std::mutex> lock(slot.mutex);
      for (size_t offset = 0; offset < 4; ++offset) {
        const size_t index = (slot.pool_head + offset) % 4;
        const std::shared_ptr<cv::Mat>& candidate = slot.buffer_pool[index];
        if (candidate.use_count() == 1) {
          target_buf = candidate;
          slot.pool_head = (index + 1) % 4;
          generation = ++slot.generation;
          break;
        }
      }
    }

    if (!target_buf) {
      std::lock_guard<std::mutex> lock(slot.mutex);
      slot.copying = false;
      ++slot.stats.skipped_count;
      return false;
    }

    // 입력 원본은 한 번만 복사하고, channel scale은 DetectionPipeline에서 한 번만
    // 적용한다. 여기서 먼저 축소하면 1.0x가 원본 검출이 아닌 보간 확대가 된다.
    if (target_buf->rows != gray.rows || target_buf->cols != gray.cols ||
        target_buf->type() != gray.type()) {
      target_buf->create(gray.rows, gray.cols, gray.type());
    }
    gray.copyTo(*target_buf);
    const Clock::time_point snapshot_ready_at = Clock::now();
    const int snapshot_copy_us = static_cast<int>(std::max<int64_t>(0,
        std::chrono::duration_cast<std::chrono::microseconds>(
            snapshot_ready_at - source_received_at).count()));

    {
      std::lock_guard<std::mutex> lock(slot.mutex);
      slot.pending = FrameSnapshot{target_buf, generation, source_received_at,
                                   snapshot_ready_at, snapshot_copy_us};
      slot.copying = false;
      ++slot.stats.clone_count;
      slot.stats.last_generation = generation;
    }
    Notify(channel);
    return true;
  }

 public:

  // dispatcher가 다음 검출을 마치면 호출한다. 처음 시작할 때도 채널별로
  // 한 번씩 요청해야 callback이 실제 프레임을 복사한다.
  void RequestFrame(int channel) {
    if (channel < 1 || channel > 4) return;
    ChannelSlot& slot = slots_[static_cast<size_t>(channel)];
    std::lock_guard<std::mutex> lock(slot.mutex);
    slot.requested = true;
  }

  bool AcquirePending(int channel, FrameSnapshot* snapshot) {
    if (snapshot == nullptr || channel < 1 || channel > 4) return false;
    ChannelSlot& slot = slots_[static_cast<size_t>(channel)];
    std::lock_guard<std::mutex> lock(slot.mutex);
    if (!slot.pending || slot.in_flight) return false;
    slot.in_flight = slot.pending;
    slot.pending = FrameSnapshot();
    slot.requested = true;
    slot.stats.in_flight = true;
    slot.stats.pending = false;
    slot.stats.in_flight_generation = slot.in_flight.generation;
    ++slot.stats.consumed_count;
    *snapshot = slot.in_flight;
    return true;
  }

  void Complete(int channel, uint64_t generation) {
    if (channel < 1 || channel > 4) return;
    ChannelSlot& slot = slots_[static_cast<size_t>(channel)];
    {
      std::lock_guard<std::mutex> lock(slot.mutex);
      if (slot.in_flight && slot.in_flight.generation == generation) {
        slot.in_flight = FrameSnapshot();
        ++slot.stats.completed_count;
      }
      slot.stats.in_flight = false;
      slot.stats.in_flight_generation = 0;
      if (!slot.pending) slot.requested = true;
    }
  }

  uint64_t GetGeneration(int channel) const {
    if (channel < 1 || channel > 4) return 0;
    const ChannelSlot& slot = slots_[static_cast<size_t>(channel)];
    std::lock_guard<std::mutex> lock(slot.mutex);
    return slot.generation;
  }

  ChannelStats GetStats(int channel) const {
    if (channel < 1 || channel > 4) return ChannelStats();
    const ChannelSlot& slot = slots_[static_cast<size_t>(channel)];
    std::lock_guard<std::mutex> lock(slot.mutex);
    ChannelStats result = slot.stats;
    result.pending = static_cast<bool>(slot.pending);
    result.in_flight = static_cast<bool>(slot.in_flight);
    result.pending_generation = slot.pending.generation;
    result.in_flight_generation = slot.in_flight.generation;
    return result;
  }

  void SetNotify(NotifyFn notify) {
    std::lock_guard<std::mutex> lock(notify_mutex_);
    notify_ = std::move(notify);
  }

  void SetFrameMode(TestFrameMode mode) {
    std::lock_guard<std::mutex> lock(mode_mutex_);
    mode_ = mode;
    cached_marker_frame_.reset();
  }

  TestFrameMode GetFrameMode() const {
    std::lock_guard<std::mutex> lock(mode_mutex_);
    return mode_;
  }

  void SetMarkerDictionary(ArucoDictionaryName dictionary) {
    std::lock_guard<std::mutex> lock(mode_mutex_);
    if (marker_dictionary_ != dictionary) cached_marker_frame_.reset();
    marker_dictionary_ = dictionary;
  }

 private:
  struct ChannelSlot {
    mutable std::mutex mutex;
    std::shared_ptr<cv::Mat> buffer_pool[4];
    size_t pool_head = 0;
    FrameSnapshot pending;
    FrameSnapshot in_flight;
    uint64_t generation = 0;
    bool requested = false;
    bool copying = false;
    Clock::time_point last_clone_at = Clock::time_point::min();
    ChannelStats stats;

    ChannelSlot() {
      // 입력 해상도는 source마다 다르므로 첫 프레임 크기로만 버퍼를 할당한다.
      for (int i = 0; i < 4; ++i) {
        buffer_pool[i] = std::make_shared<cv::Mat>();
      }
    }
  };

  FrameSnapshot MakeSyntheticSnapshot(TestFrameMode mode) const {
    std::lock_guard<std::mutex> lock(mode_mutex_);
    if (mode == TestFrameMode::kWhite) {
      return FrameSnapshot{std::make_shared<const cv::Mat>(1592, 2560, CV_8UC1, cv::Scalar(255)),
                           0, Clock::now()};
    }
    if (mode == TestFrameMode::kBlack) {
      return FrameSnapshot{std::make_shared<const cv::Mat>(1592, 2560, CV_8UC1, cv::Scalar(0)),
                           0, Clock::now()};
    }
    if (mode == TestFrameMode::kMarker) {
      if (!cached_marker_frame_) {
        cv::Mat marker(1592, 2560, CV_8UC1, cv::Scalar(255));
        cv::Mat marker_image;
        // OpenCV 5 moved marker generation onto Dictionary; the target SDK still
        // exposes the legacy free function, so keep both APIs available.
#if CCTV_ARUCO_MODERN_API
        const cv::aruco::Dictionary dictionary =
            cv::aruco::getPredefinedDictionary(marker_dictionary_);
        dictionary.generateImageMarker(0, 500, marker_image, 1);
#else
        const cv::Ptr<cv::aruco::Dictionary> dictionary =
            cv::aruco::getPredefinedDictionary(marker_dictionary_);
        cv::aruco::drawMarker(dictionary, 0, 500, marker_image, 1);
#endif
        marker_image.copyTo(marker(cv::Rect(1030, 546, 500, 500)));
        cached_marker_frame_ = std::make_shared<const cv::Mat>(marker);
      }
      return FrameSnapshot{cached_marker_frame_, 0, Clock::now()};
    }
    return FrameSnapshot();
  }

  void Notify(int channel) {
    NotifyFn notify;
    {
      std::lock_guard<std::mutex> lock(notify_mutex_);
      notify = notify_;
    }
    if (notify) notify(channel);
  }

  mutable std::array<ChannelSlot, 5> slots_;
  mutable std::mutex mode_mutex_;
  TestFrameMode mode_ = TestFrameMode::kCamera;
  ArucoDictionaryName marker_dictionary_ = cv::aruco::DICT_4X4_50;
  mutable std::shared_ptr<const cv::Mat> cached_marker_frame_;
  mutable std::mutex notify_mutex_;
  NotifyFn notify_;
};
