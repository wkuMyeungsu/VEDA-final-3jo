#pragma once

#include <map>
#include <memory>

#include "component.h"
#include "i_detector_manager.h"
#include "metadata_format.h"
#include "dispatcher_serialize.h"
#include "channel_worker.h"

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

 private:
  std::string setting_changed_time_;
  std::map<int, std::unique_ptr<ChannelWorker>> workers_; // 채널번호 -> 워커
};
