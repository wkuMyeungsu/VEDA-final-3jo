#include "detection_pipeline.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <exception>
#include <stdexcept>

#include <opencv2/imgproc.hpp>

#if defined(__linux__)
#include <time.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

int ElapsedUs(const Clock::time_point& begin, const Clock::time_point& end) {
  return static_cast<int>(std::max<int64_t>(0,
      std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count()));
}

long long ThreadCpuUs() {
#if defined(__linux__)
  struct timespec timespec_value;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &timespec_value) == 0) {
    return static_cast<long long>(timespec_value.tv_sec) * 1000000LL +
           static_cast<long long>(timespec_value.tv_nsec) / 1000LL;
  }
#endif
  return 0;
}

}  // namespace

PipelineOutput DetectionPipeline::Run(const RawFrameStore::FrameSnapshot& snapshot,
                                      const ArucoDetector& detector, double scale,
                                      const SendFn& send) {
  PipelineOutput output;
  output.scale = scale;
  if (snapshot.empty()) {
    output.error = "raw frame unavailable";
    return output;
  }
  if (!(scale == 1.0 || scale == 0.5 || scale == 0.25)) {
    output.error = "scale must be one of 1.0, 0.5, 0.25";
    return output;
  }

  const Clock::time_point pipeline_begin = Clock::now();
  const long long cpu_begin = ThreadCpuUs();

  try {
    thread_local cv::Mat tls_scaled_buffer;
    cv::Mat processed = *snapshot.image;
    const int target_cols = std::max(1, static_cast<int>(std::lround(processed.cols * scale)));
    const int target_rows = std::max(1, static_cast<int>(std::lround(processed.rows * scale)));

    if (processed.cols != target_cols || processed.rows != target_rows) {
      const Clock::time_point begin = Clock::now();
      if (tls_scaled_buffer.cols != target_cols || tls_scaled_buffer.rows != target_rows) {
        tls_scaled_buffer.create(target_rows, target_cols, processed.type());
      }
      cv::resize(processed, tls_scaled_buffer, cv::Size(target_cols, target_rows), 0, 0, cv::INTER_LINEAR);
      processed = tls_scaled_buffer;
      output.timing.resize_us = ElapsedUs(begin, Clock::now());
    } else {
      output.timing.resize_us = 0;
    }
    // 검출은 축소 영상에서 수행해도 결과 corner는 아래 복원 단계에서
    // 원본 해상도로 환산하므로 외부에 노출하는 좌표 공간은 항상 raw다.
    output.coordinate_space = "raw";
    output.processed_width = processed.cols;
    output.processed_height = processed.rows;

    const Clock::time_point detect_begin = Clock::now();
    output.result = detector.Detect(processed);
    output.timing.detect_us = ElapsedUs(detect_begin, Clock::now());

    const Clock::time_point restore_begin = Clock::now();
    // 입력 snapshot 좌표계로 복원한다. source 해상도와 가로세로 비율을 가정하지 않는다.
    if (processed.cols != snapshot.width() || processed.rows != snapshot.height()) {
      const float inverse_x = static_cast<float>(snapshot.width()) / processed.cols;
      const float inverse_y = static_cast<float>(snapshot.height()) / processed.rows;
      for (std::vector<cv::Point2f>& corners : output.result.corners) {
        for (cv::Point2f& point : corners) {
          point.x *= inverse_x;
          point.y *= inverse_y;
        }
      }
    }
    output.timing.coordinate_restore_us = ElapsedUs(restore_begin, Clock::now());

    if (send) {
      const Clock::time_point send_begin = Clock::now();
      send(output.result);
      output.timing.send_us = ElapsedUs(send_begin, Clock::now());
    }
  } catch (const std::exception& exception) {
    output.error = exception.what();
  } catch (...) {
    output.error = "unknown detection pipeline error";
  }

  const Clock::time_point pipeline_end = Clock::now();
  output.timing.processing_total_us = ElapsedUs(pipeline_begin, pipeline_end);
  const long long cpu_end = ThreadCpuUs();
  output.timing.thread_cpu_us = cpu_end >= cpu_begin ? static_cast<int>(cpu_end - cpu_begin) : 0;
  return output;
}
