#pragma once

#include <array>
#include <string>
#include <vector>

#include <opencv2/aruco/charuco.hpp>
#include <opencv2/core.hpp>

#include "camera_credentials.h"
#include "component.h"
#include "dispatcher_serialize.h"
#include "i_sample_component.h"

class SampleComponent : public Component, public ISampleComponent {
 public:
  SampleComponent();
  SampleComponent(ClassID id, const char* name);
  virtual ~SampleComponent();
  bool ProcessAEvent(Event* event) override;

 protected:
  bool Initialize() override;

 private:
  static constexpr int kChannelCount = 4;
  static constexpr int kMinCapturesForCalibration = 10;
  static constexpr int kMinCharucoCornersPerFrame = 4;

  struct BoardConfig {
    int squares_x = 0;
    int squares_y = 0;
    float square_length_m = 0.f;
    float marker_length_m = 0.f;
    bool configured = false;
  };

  struct CalibrationResult {
    bool valid = false;
    double rms = 0.0;
    cv::Size image_size;
    cv::Mat camera_matrix;
    cv::Mat dist_coeffs;
    std::vector<double> per_view_errors_px;
  };

  struct ChannelSession {
    std::vector<cv::Mat> charuco_corners;  // 캡처마다 하나씩 (Nx1 CV_32FC2)
    std::vector<cv::Mat> charuco_ids;      // 캡처마다 하나씩 (Nx1 CV_32SC1)
    cv::Size image_size;
    CalibrationResult last_result;
  };

  // 한 번의 검출(스냅샷 요청 + 마커/ChArUco 코너 인식) 결과. 저장 여부와 무관하게 채워짐.
  struct DetectionOutcome {
    bool ok = false;
    std::string error;
    cv::Size image_size;
    std::vector<int> marker_ids;
    std::vector<std::vector<cv::Point2f>> marker_corners;
    cv::Mat charuco_corners;
    cv::Mat charuco_ids;
  };

  bool HandleHttpRequest(Event* event);
  void RegisterURI();

  void HandleSetBoard(OpenAppSerializable* oas);
  void HandleDetect(OpenAppSerializable* oas);
  void HandleCapture(OpenAppSerializable* oas);
  void HandleStatus(OpenAppSerializable* oas);
  void HandleDiscard(OpenAppSerializable* oas);
  void HandleReset(OpenAppSerializable* oas);

  bool RunDetection(int channel, DetectionOutcome& out);
  void WriteDetectionJson(JsonUtility::JsonDocument& doc, const DetectionOutcome& outcome);

  bool FetchSnapshot(int channel, std::vector<unsigned char>& out_jpeg, std::string& out_error);
  cv::Ptr<cv::aruco::CharucoBoard> GetBoard();

  int ChannelIndex(int channel_number) const;
  std::string GetQueryParam(const std::string& query, const std::string& key);

 private:
  BoardConfig board_config_;
  cv::Ptr<cv::aruco::Dictionary> dictionary_;
  cv::Ptr<cv::aruco::CharucoBoard> board_;
  CameraCredentials camera_credentials_;

  std::array<ChannelSession, kChannelCount> sessions_;
};
