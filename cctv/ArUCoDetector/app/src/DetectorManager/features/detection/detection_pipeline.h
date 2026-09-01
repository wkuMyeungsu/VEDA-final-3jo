#pragma once

#include <functional>
#include <string>
#include <vector>

#include "aruco_detector.h"
#include "raw_frame_store.h"

// 운영 dispatcher와 비교 테스트가 같은 순서와 같은 좌표 복원 규칙을 사용하도록
// 프레임 하나의 모든 처리 구간을 이 객체에 모은다.
struct PipelineTiming {
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
};

struct PipelineOutput {
  DetectionResult result;
  PipelineTiming timing;
  int processed_width = 0;
  int processed_height = 0;
  double scale = 1.0;
  std::string coordinate_space = "raw";
  std::string error;
};

class DetectionPipeline {
 public:
  using SendFn = std::function<void(const DetectionResult&)>;

  // detector는 한 worker가 소유한다. OpenCV detector 인스턴스를 worker 사이에서
  // 공유하지 않아 parameters/CLAHE/LUT 재사용과 동시성 안전성을 함께 보장한다.
  static PipelineOutput Run(const RawFrameStore::FrameSnapshot& snapshot,
                            const ArucoDetector& detector, double scale,
                            const SendFn& send);
};
