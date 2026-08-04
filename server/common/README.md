# common

입력·로직·앱에서 함께 사용하는 구조체, 타입, 인터페이스와 유틸리티를 둔다.

현재 공통 타입은 `types.hpp`에 정의한다.

- `PixelPoint`: 카메라 원본 픽셀 좌표
- `WorldPoint`: 바닥 기준 공통 좌표(m)
- `ArucoMarker`: 마커 ID와 원본 순서의 코너 4개
- `ArucoFrame`: 채널, 카메라·서버 시각, 마커 목록
