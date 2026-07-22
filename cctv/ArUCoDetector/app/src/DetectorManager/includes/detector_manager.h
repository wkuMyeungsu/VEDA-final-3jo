#pragma once

#include "component.h"
#include "i_detector_manager.h"
#include "metadata_format.h"

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
  void SendMetadata(const std::vector<int>& ids, const std::vector<std::vector<cv::Point2f>>& corners);
  void ProcessMetadata(Event* event);

 private:
  std::string setting_changed_time_;
};
