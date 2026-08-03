cmake_minimum_required(VERSION 3.15)
project(nearest_person_selector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 선택 로직(헤더 + 구현)만 담은 정적 라이브러리.
# 실행파일과 다른 컴포넌트(judgment_pipeline)가 같은 구현을 공유하도록 하고,
# main()은 각 실행파일 쪽에만 둔다.
add_library(nearest_person_selector STATIC
    nearest_person_selector.cpp
)

# ../judgment은 nearest_person_selector.h가 WorldPoint를 danger_judgment_engine.h에서
# 가져오기 때문에 필요하다. 타입 정의만 쓰고 링크할 심볼은 없으므로
# danger_judgment_engine 라이브러리 타깃에 대한 의존성은 아니다
# (그래서 이 디렉터리만 단독으로 configure해도 빌드된다).
target_include_directories(nearest_person_selector PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../judgment
)

# 독립 실행용 더미 시나리오 실행파일. 라이브러리 타깃과 이름이 겹칠 수 없어
# danger_judgment_engine/danger_engine과 같은 방식으로 실행파일만 다른 이름을 쓴다.
add_executable(nearest_person_demo
    nearest_person_selector_main.cpp
)
target_link_libraries(nearest_person_demo PRIVATE nearest_person_selector)

# 크로스카메라 Re-ID 더미 시나리오 실행파일 (자체 main 보유, 외부 의존성 없음).
# [주의] nearest_person_selector와 링크하면 안 된다 - euclideanDistance가 양쪽에 정의돼 있다.
add_executable(cross_camera_reid_demo cross_camera_reid.cpp)
