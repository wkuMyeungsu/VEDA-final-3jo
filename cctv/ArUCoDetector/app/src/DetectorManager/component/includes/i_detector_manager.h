#pragma once

#include "typedef_application.h"

constexpr ClassID _DetectorManager_Id = GET_CLASS_UID(_ELayer_Application::_eDetectorManager);

class IDetectorManager {
 public:
  enum class EEventType { eBegin = _DetectorManager_Id, eEnd };
};
