#pragma once

#include <string>
#include <vector>

#include "component.h"
#include "i_sample_component.h"
#include "metadata_format.h"

// 실물 카메라/마커 없이 ArUco_Detection과 동일한 형식의 메타데이터를 랜덤으로 만들어
// 전송 파이프라인(수신측)만 테스트하기 위한 더미 앱. 이미지 처리를 하지 않으므로
// OpenCV 의존성이 전혀 없음 (좌표는 MarkerMetadataFormat::Point2f 사용).
class SampleComponent : public Component, public ISampleComponent {
 public:
  SampleComponent();
  SampleComponent(ClassID id, const char* name);
  virtual ~SampleComponent();
  bool ProcessAEvent(Event* event) override;

 protected:
  bool Initialize() override;

 private:
  bool HandleHttpRequest(Event* event);
  void RegisterURI();

  // XML 포맷 자체는 metadata_format.h/.cc로 분리됨 (SendMetadata는 그걸 호출만 함)
  void SendMetadata(const std::vector<int>& ids, const std::vector<std::vector<MarkerMetadataFormat::Point2f>>& corners);

  // MetadataManager가 실제로 우리가 보낸 데이터를 받아 채널로 되돌려주는지
  // 콘솔 로그로 직접 확인하기 위한 구독용 핸들러 (metadata_sample 참고)
  void ProcessMetadata(Event* event);
};
