cmake_minimum_required(VERSION 3.10)
project(promeki VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(PROMEKI_SOURCES
    src/unittest.cpp
    src/string.cpp
    src/error.cpp
    src/shareddata.cpp
    src/point2d.cpp
)

add_library(promeki SHARED ${PROMEKI_SOURCES})

add_executable(unittest tests/unittest.cpp)
target_link_libraries(unittest PRIVATE promeki)
add_test(NAME unittest COMMAND unittest)
 
