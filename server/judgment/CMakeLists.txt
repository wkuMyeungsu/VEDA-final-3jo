cmake_minimum_required(VERSION 3.15)
project(danger_judgment_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ResultPublisher는 POSIX 소켓(arpa/inet.h 등) + std::thread를 사용한다.
# -> pthread 링크가 필요하며 Windows/MSVC 네이티브 빌드는 지원하지 않는다(POSIX 전용).
# THREADS_PREFER_PTHREAD_FLAG를 켜면 -lpthread 대신 -pthread 플래그로 링크한다.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# 엔진 로직(헤더 + 구현)만 담은 정적 라이브러리.
# 실행파일과 테스트가 같은 구현을 공유하도록 하고, main()은 각 실행파일 쪽에만 둔다.
add_library(danger_judgment_engine STATIC
    danger_judgment_engine.cpp
)
target_include_directories(danger_judgment_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 상류(nearest_person_selector 결과) -> CameraInput 배선 glue.
# 판정 로직 없이 매핑 + evaluate() 호출만 하므로 엔진 라이브러리에만 의존한다.
# (nearest_person_selector.cpp는 아직 헤더가 없고 자체 main()을 갖고 있어 링크하지 않는다.
#  결과 타입은 judgment_pipeline.h가 미러링해 둔 상태다.)
add_library(judgment_pipeline STATIC
    judgment_pipeline.cpp
)
target_include_directories(judgment_pipeline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(judgment_pipeline PUBLIC danger_judgment_engine)

# 실행파일은 main()만 갖고 엔진은 라이브러리로 링크한다.
# ResultPublisher.h는 헤더 온리이므로 소스 추가 없이 include 경로만 있으면 된다(같은 디렉터리).
add_executable(danger_engine
    danger_judgment_engine_main.cpp
)
target_link_libraries(danger_engine PRIVATE danger_judgment_engine Threads::Threads)

# ResultPublisher 송신 큐 스트레스 테스트 (루프백 TCP 사용, 종료코드 0=성공)
add_executable(test_result_publisher
    test_result_publisher.cpp
)
target_link_libraries(test_result_publisher PRIVATE Threads::Threads)

# 예외처리 트리거 검증 테스트 (경계값 + 우선순위 매트릭스, 종료코드 0=성공)
# 엔진 라이브러리를 링크해서 쓴다. 엔진 main()은 danger_judgment_engine_main.cpp에만
# 있으므로 이 테스트의 main()과 충돌하지 않는다.
add_executable(test_exception_trigger
    test_exception_trigger.cpp
)
target_link_libraries(test_exception_trigger PRIVATE danger_judgment_engine Threads::Threads)
