#pragma once

#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <mutex>

#include "component.h"
#include "i_detector_manager.h"
#include "metadata_format.h"
#include "dispatcher_serialize.h"
#include "channel_worker.h"
#include "aruco_detector.h"
#include "raw_frame_store.h"
#include "detection_slot_limiter.h"

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
  void HandleGetLogs(OpenAppSerializable* oas);   // GET /logs — 최근 raw 로그 텍스트
  void ProcessRawVideo(Event* event);  // raw 비디오 프레임(eVideoRawData) -> 검출·전송
  void AppendLog(const std::string& line);        // 로그 링버퍼에 한 줄 추가 (UI 노출용)

 private:
  std::string setting_changed_time_;
  std::map<int, std::unique_ptr<ChannelWorker>> workers_; // 채널번호 -> 워커
  std::chrono::steady_clock::time_point workers_start_time_;  // 워커 기동 시각 (uptime 계산용)

  // raw 비디오(push) 프레임을 채널별로 보관 → 각 ChannelWorker의 FrameSource가 여기서 읽는다.
  RawFrameStore raw_store_;

  DetectionSlotLimiter slot_limiter_{1};  // permit = 1 (코어 1개는 프레임워크 용)

  // 상태 모니터링 UI에 띄울 최근 로그 (링버퍼). GET /logs 로 노출.
  std::mutex               logs_mtx_;
  std::deque<std::string>  recent_logs_;
  static constexpr size_t  kMaxLogs = 200;
};
