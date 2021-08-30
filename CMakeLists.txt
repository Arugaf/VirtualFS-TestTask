cmake_minimum_required(VERSION 3.20)
project(test_case_vfs)

include_directories(include)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

add_executable(test_case_vfs main.cpp)
