#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <opencv2/core.hpp>

// SPMgrVideoRaw가 밀어주는 raw 비디오 프레임을 채널별로 안전하게 보관하는 스레드-안전 저장소.
// - SPMgrVideoRaw 수신 스레드(ProcessRawVideo): Put(ch, gray) 호출
// - 각 채널의 ChannelWorker 스레드: Get(ch) 호출
// 채널별로 별도 lock과 last_put_time을 유지하여 채널 간 경합을 방지한다.
class RawFrameStore {
 public:
  void Put(int channel, const cv::Mat& gray) {
    if (gray.empty()) return;
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lk(slots_[channel].mtx);
    // 200ms 스로틀링 (너무 빠른 저장 방지)
    if (slots_[channel].has_frame &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - slots_[channel].last_put_time).count() < 200) {
      return;
    }
    slots_[channel].frame = gray.clone();
    slots_[channel].last_put_time = now;
    slots_[channel].has_frame = true;
  }

  cv::Mat Get(int channel) {
    std::lock_guard<std::mutex> lk(slots_[channel].mtx);
    if (!slots_[channel].has_frame) {
      return cv::Mat();
    }
    return slots_[channel].frame.clone();
  }

 private:
  struct ChannelSlot {
    std::mutex mtx;
    cv::Mat frame;
    std::chrono::steady_clock::time_point last_put_time;
    bool has_frame = false;
  };

  std::map<int, ChannelSlot> slots_;
};
