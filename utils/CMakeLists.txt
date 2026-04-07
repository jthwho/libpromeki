# Promeki Utilities
#
# This directory contains utility applications that serve as both functional
# demonstrations of the promeki library and tools for testing.
#
# When built standalone (outside the promeki source tree), this project
# uses find_package(promeki) to locate an installed copy of the library,
# just as any external consumer would.  When built as a subdirectory of
# the main promeki project, the library targets are already available.

cmake_minimum_required(VERSION 3.22)
project(promeki-utils LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# When built standalone, find the installed promeki library.
# When built as part of the main project, the targets already exist.
if(NOT TARGET promeki::promeki)
    find_package(promeki REQUIRED)
endif()

add_subdirectory(promeki-info)

if(PROMEKI_ENABLE_PROAV)
    add_subdirectory(imgtest)
    add_subdirectory(testrender)
    if(PROMEKI_ENABLE_NETWORK)
        add_subdirectory(vidgen)
    endif()
    if(PROMEKI_ENABLE_CSC)
        add_subdirectory(cscbench)
    endif()
    if(PROMEKI_BUILD_SDL)
        add_subdirectory(mediaplay)
    endif()
endif()
