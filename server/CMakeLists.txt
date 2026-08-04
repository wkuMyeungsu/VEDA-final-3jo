cmake_minimum_required(VERSION 3.15)
project(forklift_safety_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(forklift_safety_server app/main.cpp)
target_include_directories(forklift_safety_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

