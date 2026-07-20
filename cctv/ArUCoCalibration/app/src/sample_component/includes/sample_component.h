#pragma once

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

  struct BoardConfig {
    int squares_x = 0;
    int squares_y = 0;
    float square_length_m = 0.f;
    float marker_length_m = 0.f;
    bool configured = false;
  };

  // 한 번의 검출(스냅샷 요청 + 마커/ChArUco 코너 인식) 결과.
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
};
