# third_party

외부 라이브러리를 소스째로 들여온 곳. 여기 파일은 우리가 작성한 코드가 아니므로
직접 수정하지 않는다(수정이 필요하면 버전을 올리고 그 사실을 이 문서에 남긴다).

## nlohmann/json

- 버전: v3.11.3 (single-header 배포본)
- 출처: <https://github.com/nlohmann/json/releases/tag/v3.11.3>
  (`single_include/nlohmann/json.hpp` 파일 하나)
- 라이선스: MIT (파일 상단 헤더 주석 참고)
- 용도: `apps/main_app/src/config/safety_server_config.*`의 단말 설정 JSON 파싱.

### 왜 시스템 패키지(`nlohmann-json3-dev`)가 아니라 벤더링인가

서버는 라즈베리파이 실기와 팀원 개발 PC 양쪽에서 빌드되는데, 설정 파일 하나 읽자고
전원이 apt 패키지를 따로 깔아야 하면 빌드가 환경마다 갈린다. 헤더 1개짜리
라이브러리라 레포에 그대로 두는 편이 재현성이 좋다.

`find_package(nlohmann_json)`으로 바꾸고 싶다면 CMakeLists.txt의 `server_config`
타깃에서 이 디렉터리 include를 걷어내면 된다.

### 주의

- 이 헤더는 하나에 약 900KB / 2만 5천 줄이라 빌드 시간에 영향을 준다. 그래서
  `server_config` 한 타깃 안에서만 include하고(PRIVATE), 다른 계층 헤더에는
  노출하지 않는다. `safety_server_config.hpp`가 nlohmann 타입을 공개 API에 쓰지 않는
  것도 같은 이유다.
- 서버의 다른 JSON 처리(판정 결과 직렬화 `toJson()`)는 기존
  수작업 방식 그대로다. 이미 확정된 하류 계약이라 굳이 갈아엎지 않았다.
