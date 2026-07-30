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

# 상류 컴포넌트(최근접 사람 선택)를 라이브러리로 가져온다.
# 별도 프로젝트로 두고 여기서 끌어오는 이유: 그쪽도 자체 실행파일(nearest_person_demo)을
# 갖고 있어 독립 configure가 가능해야 하고, 엔진처럼 구현을 한 군데서 공유해야 한다.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../tracking
                 ${CMAKE_CURRENT_BINARY_DIR}/tracking)

# 상류(nearest_person_selector 결과) -> CameraInput 배선 glue.
# 판정 로직 없이 매핑 + evaluate() 호출만 하므로 두 컴포넌트의 라이브러리에만 의존한다.
# nearest_person_selector는 PUBLIC으로 링크해서, 이걸 쓰는 쪽(테스트/향후 실제 main)이
# selectNearestPerson()까지 바로 호출할 수 있게 한다.
add_library(judgment_pipeline STATIC
    judgment_pipeline.cpp
)
target_include_directories(judgment_pipeline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(judgment_pipeline PUBLIC danger_judgment_engine nearest_person_selector)

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

# 배선 glue 검증 테스트 (매핑 규칙 + processFrame end-to-end, 종료코드 0=성공)
# judgment_pipeline이 엔진 라이브러리를 PUBLIC으로 링크하므로 엔진은 전이로 따라온다.
add_executable(test_judgment_pipeline
    test_judgment_pipeline.cpp
)
target_link_libraries(test_judgment_pipeline PRIVATE judgment_pipeline Threads::Threads)
