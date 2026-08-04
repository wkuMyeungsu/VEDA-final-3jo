cmake_minimum_required(VERSION 3.15)
project(forklift_safety_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(forklift_safety_server app/main.cpp)
target_include_directories(forklift_safety_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

enable_testing()

add_executable(common_types_test tests/common_types_test.cpp)
target_include_directories(common_types_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME common_types_test COMMAND common_types_test)
