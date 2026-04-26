# Promeki Demos
#
# This directory contains demonstration applications that showcase specific
# aspects of the promeki library.  Unlike the utilities (which provide
# functional use beyond the library), demos exist to help users learn and
# understand the library's APIs.
#
# When built standalone (outside the promeki source tree), this project
# uses find_package(promeki) to locate an installed copy of the library,
# just as any external consumer would.  When built as a subdirectory of
# the main promeki project, the library targets are already available.

cmake_minimum_required(VERSION 3.22)
project(promeki-demos LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# When built standalone, find the installed promeki library.
# When built as part of the main project, the targets already exist.
if(NOT TARGET promeki::promeki)
    find_package(promeki REQUIRED)
endif()

# Inherit the project's warning policy when built in-tree.  Standalone
# builds (find_package) skip this — the warnings interface lib is not
# part of the installed/exported promeki target set.
if(TARGET promeki::warnings)
    link_libraries(promeki::warnings)
endif()

add_subdirectory(crashtest)
add_subdirectory(http-explorer)
add_subdirectory(tui-demo)
add_subdirectory(tui-event-test)
add_subdirectory(promeki-pipeline)
