#pragma once
//
// ===== 테스트 전용 (나중에 통째로 삭제) =====
// /detectonce 엔드포인트 처리. 스냅샷 1장을 떠서 검출 파이프라인 전체가
// 실제로 도는지 수동으로 확인하기 위한 임시 핸들러.
// channel_worker(자동 폴링)가 완성되면 이 파일과 CMakeLists 등록,
// detector_manager.cc의 연결 한 줄을 지우면 깔끔하게 제거된다.
//
#include <functional>
#include <vector>

#include <opencv2/core.hpp>

class OpenAppSerializable;

namespace DebugDetectHandler {

// 검출 결과를 메타데이터로 실제 전송하는 콜백. DetectorManager::SendMetadata를 감싼다
// (SendNoReplyEvent가 컴포넌트 멤버라 debug 핸들러가 직접 못 부르므로 콜백으로 주입).
using SendMetadataFn =
    std::function<void(const std::vector<int>& ids, const std::vector<std::vector<cv::Point2f>>& corners)>;

// 스냅샷 1장 → (해상도 맞으면 undistort) → 검출 → 메타데이터 전송
// → 검출 오버레이를 그린 프리뷰 이미지(base64)를 포함한 JSON 응답.
void HandleDetectOnce(OpenAppSerializable* oas, const SendMetadataFn& send_metadata);

}  // namespace DebugDetectHandler
