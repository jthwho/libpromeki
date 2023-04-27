cmake_minimum_required(VERSION 3.10)

set(LIBNAME promeki)

# Set the project name and version
project(${LIBNAME} VERSION 1.0.0)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

# List the source files explicitly
set(SOURCES
    unittest.cpp
    string.cpp
    error.cpp
    shareddata.cpp
    point2d.cpp
)

# Create a shared library target
add_library(${LIBNAME} SHARED ${SOURCES})


