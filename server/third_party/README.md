# third_party

서버가 직접 관리하지 않는 외부 라이브러리를 저장하는 영역이다. 외부 코드는
직접 수정하지 않고, 버전 변경이 필요할 때 새 배포본과 라이선스 정보를 함께
갱신한다.

## 한눈에 보기

- 현재 외부 라이브러리: `nlohmann/json`
- 버전: `v3.11.3`
- 형태: single-header `server/third_party/nlohmann/json.hpp`
- 라이선스: MIT
- 사용 영역: `apps/main_app/src/config_loader/safety_server_config.cpp`
- 공개 API 노출: 없음
- 업데이트 방식: 원본 배포본 교체 + 버전·출처·라이선스 확인

## 라이브러리

### 목록

- JSON 설정 파싱
- `server_config` 타깃 내부 전용 include
- 안전 서버 설정 구조체를 통한 타입 경계 유지

### 상세

```text
server/third_party/nlohmann/json.hpp
  → apps/main_app/src/config_loader/safety_server_config.cpp
  → SafetyServerConfig
```

판정 결과 `toJson()`과 MQTT payload는 기존 프로토콜 계약을 유지하기 위해
별도 수작업 직렬화를 사용한다. `nlohmann/json`을 전체 네트워크 계층에
노출하지 않는다.

## 버전

| 항목 | 값 |
| --- | --- |
| 라이브러리 | nlohmann/json |
| 버전 | v3.11.3 |
| 출처 | [GitHub release](https://github.com/nlohmann/json/releases/tag/v3.11.3) |
| 파일 | `nlohmann/json.hpp` |
| 라이선스 | MIT, 파일 상단 헤더 참조 |

## 관리

### 목록

- 원본 배포본 확인
- 버전·라이선스 기록
- include 범위 확인
- 빌드·CTest 검증

### 상세

- 헤더 파일을 기능 코드처럼 직접 수정하지 않는다.
- 버전을 올릴 때 이 README의 버전·출처를 함께 수정한다.
- 공개 헤더에서 nlohmann 타입을 반환하거나 인자로 노출하지 않는다.
- 변경 후 `server_config` 빌드와 안전 서버 설정 테스트를 실행한다.

single-header 라이브러리를 저장소에 포함하는 이유는 개발 환경마다 시스템
패키지 설치 여부가 달라지는 것을 줄이고, 설정 파서의 빌드 재현성을 유지하기
위해서다.

## 참고

- 외부 라이브러리 원본과 라이선스는 파일 상단 헤더를 우선한다.
- 서버 전체 구성과 빌드는 [상위 서버 문서](../README.md)를 참고한다.
