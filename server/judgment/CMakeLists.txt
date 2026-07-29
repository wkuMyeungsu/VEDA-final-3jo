cmake_minimum_required(VERSION 3.15)
project(danger_judgment_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ResultPublisher는 POSIX 소켓(arpa/inet.h 등) + std::thread를 사용한다.
# -> pthread 링크가 필요하며 Windows/MSVC 네이티브 빌드는 지원하지 않는다(POSIX 전용).
# THREADS_PREFER_PTHREAD_FLAG를 켜면 -lpthread 대신 -pthread 플래그로 링크한다.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(danger_engine
    danger_judgment_engine.cpp
)

# ResultPublisher.h는 헤더 온리이므로 소스 추가 없이 include 경로만 있으면 된다(같은 디렉터리).
target_link_libraries(danger_engine PRIVATE Threads::Threads)

# ResultPublisher 송신 큐 스트레스 테스트 (루프백 TCP 사용, 종료코드 0=성공)
add_executable(test_result_publisher
    test_result_publisher.cpp
)
target_link_libraries(test_result_publisher PRIVATE Threads::Threads)
