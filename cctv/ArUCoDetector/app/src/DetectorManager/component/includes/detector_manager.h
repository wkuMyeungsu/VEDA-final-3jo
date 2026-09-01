#pragma once

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "component.h"
#include "i_detector_manager.h"
#include "metadata_xml_builder.h"
#include "dispatcher_serialize.h"
#include "detection_dispatcher.h"
#include "detection_settings.h"
#include "raw_frame_store.h"
#include "features/test/test_run_controller.h"

class DetectorManager : public Component, public IDetectorManager {
 public:
  DetectorManager();
  DetectorManager(ClassID id, const char* name);
  ~DetectorManager() override;
  bool ProcessAEvent(Event* event) override;

 protected:
  bool Initialize() override;

 private:
  bool HandleHttpRequest(Event* event);
  void RegisterURI();
  std::string GetCurrentTimeToString();
  void SendMetadata(int channel, const DetectionResult& result);
  void ProcessMetadata(Event* event);
  void HandleGetSettings(OpenAppSerializable* oas);
  void HandlePostSettings(OpenAppSerializable* oas);
  void RestartWorkers();
  void HandleGetStatus(OpenAppSerializable* oas);
  void HandleGetLogs(OpenAppSerializable* oas);
  void HandleGetExport(OpenAppSerializable* oas, const std::string& kind);
  void ProcessRawVideo(Event* event);
  void AppendLog(const std::string& line);

  RawFrameStore raw_store_;
  DetectionDispatcher dispatcher_;
  std::unique_ptr<TestRunController> test_run_controller_;

  std::mutex logs_mtx_;
  std::deque<std::string> recent_logs_;
  static constexpr size_t kMaxLogs = 200;
};
