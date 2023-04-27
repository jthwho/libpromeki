cmake_minimum_required(VERSION 3.10)
project(promeki_tests VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../src promeki)

add_executable(unittest unittest.cpp)

target_link_libraries(unittest PRIVATE promeki)

