#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <opencv2/core.hpp>
#include <opencv2/aruco.hpp>

// 고정 입력 테스트 모드 (KAN-79)
enum class TestFrameMode {
  kCamera = 0,      // 실제 카메라 영상
  kWhite = 1,       // 고정 흰 도화지 (마커 없음, 1920x1080)
  kBlack = 2,       // 고정 검은 도화지 (마커 없음, 1920x1080)
  kMarker = 3       // 고정 ArUco 마커 1개 (ID 0, 1920x1080)
};

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

  void SetFrameMode(TestFrameMode mode) {
    std::lock_guard<std::mutex> lk(mode_mtx_);
    mode_ = mode;
    cached_marker_frame_.release(); // 모드 변경 시 캐시 갱신
  }

  TestFrameMode GetFrameMode() const {
    std::lock_guard<std::mutex> lk(mode_mtx_);
    return mode_;
  }

  cv::Mat Get(int channel) {
    {
      std::lock_guard<std::mutex> lk(mode_mtx_);
      if (mode_ == TestFrameMode::kWhite) {
        return cv::Mat(1080, 1920, CV_8UC1, cv::Scalar(255));
      } else if (mode_ == TestFrameMode::kBlack) {
        return cv::Mat(1080, 1920, CV_8UC1, cv::Scalar(0));
      } else if (mode_ == TestFrameMode::kMarker) {
        if (cached_marker_frame_.empty()) {
          cached_marker_frame_ = cv::Mat(1080, 1920, CV_8UC1, cv::Scalar(255));
          auto dict = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
          cv::Mat marker_mat;
          cv::aruco::drawMarker(dict, 0, 400, marker_mat, 1);
          // 중앙에 배치 (1920x1080) -> x: 760, y: 340
          marker_mat.copyTo(cached_marker_frame_(cv::Rect(760, 340, 400, 400)));
        }
        return cached_marker_frame_.clone();
      }
    }

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
  mutable std::mutex mode_mtx_;
  TestFrameMode mode_ = TestFrameMode::kCamera;
  cv::Mat cached_marker_frame_;
};
