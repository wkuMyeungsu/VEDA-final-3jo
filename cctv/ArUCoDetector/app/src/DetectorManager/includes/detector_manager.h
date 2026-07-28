#pragma once

#include <chrono>
#include <map>
#include <memory>

#include "component.h"
#include "i_detector_manager.h"
#include "metadata_format.h"
#include "dispatcher_serialize.h"
#include "channel_worker.h"
#include "aruco_detector.h"

class DetectorManager : public Component, public IDetectorManager {
 public:
  DetectorManager();
  DetectorManager(ClassID id, const char* name);
  virtual ~DetectorManager();
  bool ProcessAEvent(Event* event) override;

 protected:
  bool Initialize() override;

 private:
  bool HandleHttpRequest(Event* event);
  void RegisterURI();
  std::string GetCurrentTimeToString();
  void SendMetadata(int channel, const std::vector<int>& ids, const std::vector<std::vector<cv::Point2f>>& corners);
  void ProcessMetadata(Event* event);
  void HandleGetSettings(OpenAppSerializable* oas);
  void HandlePostSettings(OpenAppSerializable* oas);
  void RestartWorkers();  // settings 로드 -> 워커 전부 정지 후 재생성/시작
  void HandleGetStatus(OpenAppSerializable* oas);
  void ProcessRawVideo(Event* event);  // raw 비디오 프레임(eVideoRawData) -> 검출·전송

 private:
  std::string setting_changed_time_;
  std::map<int, std::unique_ptr<ChannelWorker>> workers_; // 채널번호 -> 워커
  std::chrono::steady_clock::time_point workers_start_time_;  // 워커 기동 시각 (uptime 계산용)

  // raw 비디오(push) 경로 전용 — ChannelWorker(폴링)를 거치지 않고 직접 검출한다.
  ArucoDetector raw_detector_;          // 기본 DICT_4X4_50
  uint64_t      raw_frame_count_ = 0;   // 스로틀 카운터
  int           raw_detect_every_ = 5;  // N프레임마다 1회만 검출 (cv5 부하 방지)
};
