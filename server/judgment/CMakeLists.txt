cmake_minimum_required(VERSION 3.15)
project(danger_judgment_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ResultPublisher는 POSIX 소켓(arpa/inet.h 등) + std::thread를 사용한다.
# -> pthread 링크가 필요하며 Windows/MSVC 네이티브 빌드는 지원하지 않는다(POSIX 전용).
# THREADS_PREFER_PTHREAD_FLAG를 켜면 -lpthread 대신 -pthread 플래그로 링크한다.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# EventLogger(판정 이벤트 로그)는 SQLite3 C API를 사용한다.
#
# 설치 (라이브러리가 없어서 아래 find_package가 실패할 때):
#   Debian/Ubuntu/Raspberry Pi OS : sudo apt install libsqlite3-dev
#   Fedora/RHEL                   : sudo dnf install sqlite-devel
#   Arch                          : sudo pacman -S sqlite
#   macOS (Homebrew)              : brew install sqlite
#   Windows (개발 확인용, vcpkg)   : vcpkg install sqlite3:x64-windows
#     -> configure 시 툴체인 지정:
#        cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
#     (단, 이 프로젝트의 나머지 부분은 POSIX 전용이라 Windows 네이티브 빌드는 지원하지 않는다.
#      Windows에서는 EventLogger/뷰어/테스트만 따로 컴파일해 확인하는 용도다.)
#
# CMake 3.14+ 기본 제공 FindSQLite3 모듈이 SQLite::SQLite3 임포트 타깃을 만들어 준다.
find_package(SQLite3 REQUIRED)

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

# SensorCollector(forklift-device 트랙, 실제 MPU6050/VL53L1X 드라이버) -> ISensorReader 어댑터.
# forklift-device/sensors 아래 드라이버/인터페이스는 전부 헤더 온리라 별도 소스 없이
# include 경로만 추가하면 된다. main_test.cpp / test_mpu6050_real.cpp는 자체 main()을 가진
# 독립 스모크 테스트라 이 타깃에는 포함하지 않는다(연동 시 이 타깃과 링크 충돌 발생).
# ISensorReader/SensorInput을 그대로 노출해야 호출부가 judgment_pipeline.h를 include할 수
# 있으므로 judgment_pipeline은 PUBLIC으로 링크한다.
add_library(sensor_collector_reader STATIC
    sensor_collector_reader.cpp
)
target_include_directories(sensor_collector_reader PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../forklift-device/sensors
)
target_link_libraries(sensor_collector_reader PUBLIC judgment_pipeline)

# 판정 이벤트 SQLite 로거.
# 헤더가 std::thread 멤버와 JudgmentResult를 노출하므로 엔진/Threads/SQLite 모두 PUBLIC이다
# (이걸 링크하는 쪽이 EventLogger.h를 그대로 include할 수 있어야 한다).
add_library(event_logger STATIC
    EventLogger.cpp
)
target_include_directories(event_logger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(event_logger PUBLIC danger_judgment_engine SQLite::SQLite3 Threads::Threads)

# 서버 내부 판정 지연(t0_ingest~t2_send) CSV 로거.
# event_logger와 같은 구성 방식(헤더가 std::thread 멤버를 노출하므로 Threads는 PUBLIC).
# SQLite 의존은 없다(CSV 파일에 std::ofstream으로 직접 쓴다) - latency_stamps.h만 있으면
# 되므로 danger_judgment_engine을 링크할 필요는 없다.
add_library(latency_logger STATIC
    LatencyLogger.cpp
)
target_include_directories(latency_logger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(latency_logger PUBLIC Threads::Threads)

# 실행파일은 main()만 갖고 엔진은 라이브러리로 링크한다.
# ResultPublisher.h / ResultDispatcher.h는 헤더 온리이므로 소스 추가 없이
# include 경로만 있으면 된다(같은 디렉터리).
# event_logger/latency_logger를 링크하면 엔진/Threads는 전이로 따라온다.
add_executable(danger_engine
    danger_judgment_engine_main.cpp
)
target_link_libraries(danger_engine PRIVATE danger_judgment_engine event_logger latency_logger Threads::Threads)

# 이벤트 로그 조회용 독립 실행파일 (읽기 전용).
# 엔진과 별개로 돌리는 도구라 소켓/스레드 쪽에는 의존하지 않는다.
add_executable(event_log_viewer
    event_log_viewer_main.cpp
)
target_link_libraries(event_log_viewer PRIVATE event_logger)

# 송신기(서버 소켓·큐) + 전송 정책(변화 즉시 / 200ms 하트비트) 테스트
# (루프백 TCP 사용, 종료코드 0=성공)
# ResultDispatcher가 toJson()을 쓰므로 엔진 라이브러리를 링크한다.
add_executable(test_result_publisher
    test_result_publisher.cpp
)
target_link_libraries(test_result_publisher PRIVATE danger_judgment_engine Threads::Threads)

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

# 이벤트 로그 검증 테스트 (변화 시에만 기록 / previous_risk_level / 백프레셔, 종료코드 0=성공)
# ResultDispatcher.h(헤더 온리)를 함께 쓰므로 Threads가 필요한데 event_logger가 PUBLIC으로
# 끌고 온다. 검증용으로 DB를 직접 읽으므로 SQLite도 전이로 따라온다.
add_executable(test_event_logger
    test_event_logger.cpp
)
target_link_libraries(test_event_logger PRIVATE event_logger)

# [로컬 하드웨어 스모크 테스트] 실제 IMU/ToF 센서값이 DangerJudgmentEngine까지 정상
# 전달되는지만 확인하는 별도 실행파일. danger_engine(위 danger_judgment_engine_main.cpp)
# 타깃과는 완전히 무관하며, JudgmentPipeline/ResultPublisher/EventLogger/LatencyLogger는
# 쓰지 않는다 - 자세한 배경은 sensor_fusion_smoke_test.cpp 상단 주석 참고.
# sensor_collector_reader가 PUBLIC으로 judgment_pipeline(-> danger_judgment_engine)과
# forklift-device/sensors include 경로를 끌고 오므로 별도 include_directories 지정은
# 필요 없다. 실제 I2C 하드웨어(라즈베리파이)에서만 실행 가능하다.
add_executable(sensor_fusion_smoke_test
    sensor_fusion_smoke_test.cpp
)
target_link_libraries(sensor_fusion_smoke_test PRIVATE sensor_collector_reader)
