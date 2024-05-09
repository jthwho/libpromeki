cmake_minimum_required(VERSION 3.22)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(APP_NAME "promeki")
set(APP_VENDOR "HOWARDLOGIC")
set(APP_HUMAN_NAME "libpromeki")
set(APP_HUMAN_VENDOR "Howard Logic")
set(APP_VERSION "1.0.0")
set(APP_VERSION_RC 0) # If this is a release candidate, set this value to the release candidate version (non zero)
set(APP_VERSION_BETA 0) # If this is a beta, set this value to the beta version (non-zero)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(${APP_NAME} VERSION ${APP_VERSION})
include(CTest)

find_package(Git QUIET REQUIRED)
find_package(PNG)
find_package(Freetype)
find_package(JPEG)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE res
    OUTPUT_VARIABLE BUILD_INFO_REPOIDENT
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set_property(GLOBAL APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/.git/index"
)

# Capture any other build info variables
# Must be below project()
if(NOT ${APP_VERSION_RC} EQUAL "0") 
    set(BUILD_INFO_VERSION "${APP_VERSION}-rc${APP_VERSION_RC}")
elseif(NOT ${APP_VERSION_BETA} EQUAL "0")
    set(BUILD_INFO_VERSION "${APP_VERSION}-beta${APP_VERSION_BETA}")
else()
    set(BUILD_INFO_VERSION "${APP_VERSION}")
endif()
set(BUILD_INFO_VERSION_BETA "${APP_VERSION_BETA}")
set(BUILD_INFO_VERSION_RC "${APP_VERSION_RC}")
set(BUILD_INFO_NAME "${APP_HUMAN_NAME}")
string(TIMESTAMP BUILD_INFO_DATE "%Y-%m-%d")
string(TIMESTAMP BUILD_INFO_TIME "%H:%M:%S")
set(BUILD_INFO_TYPE "${CMAKE_BUILD_TYPE}")
cmake_host_system_information(RESULT BUILD_INFO_HOSTNAME QUERY HOSTNAME)
configure_file(src/buildinfo.cpp.in buildinfo.cpp)

# Make sure we regenerate the buildinfo.cpp file each time we build.
add_custom_target(
    libpromeki_invalidate_build_info 
    ALL
    COMMAND ${CMAKE_COMMAND} -E touch ${PROJECT_SOURCE_DIR}/src/buildinfo.cpp.in
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(PROMEKI_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/buildinfo.cpp
    src/application.cpp
    src/objectbase.cpp
    src/system.cpp
    src/util.cpp
    src/rational.cpp
    src/fourcc.cpp
    src/array.cpp
    src/matrix.cpp
    src/unittest.cpp
    src/list.cpp
    src/string.cpp
    src/stringlist.cpp
    src/regex.cpp
    src/error.cpp
    src/uuid.cpp
    src/shareddata.cpp
    src/point.cpp
    src/line.cpp
    src/size2d.cpp
    src/mempool.cpp
    src/logger.cpp
    src/ansistream.cpp
    src/fileinfo.cpp
    src/datetime.cpp
    src/timestamp.cpp
    src/timecode.cpp
    src/variant.cpp
    src/metadata.cpp
    src/memspace.cpp
    src/buffer.cpp
    src/paintengine.cpp
    src/pixelformat.cpp
    src/pixelformat_rgb8.cpp
    src/pixelformat_rgba8.cpp
    src/pixelformat_jpeg.cpp
    src/imagedesc.cpp
    src/image.cpp
    src/audiodesc.cpp
    src/audio.cpp
    src/audiofilefactory.cpp
    src/audiofile.cpp
    src/audiogen.cpp
    src/audioblock.cpp
    src/matrix3x3.cpp
    src/colorspaceconverter.cpp
    src/imagefileio.cpp
    src/imagefile.cpp
    src/xyzcolor.cpp
    src/ciepoint.cpp
#    src/colorspace.cpp
    src/cmdlineparser.cpp
    src/file.cpp
    src/numname.cpp
    src/numnameseq.cpp
    src/signal.cpp
    src/slot.cpp
    src/frame.cpp
    src/codec.cpp
    src/pidcontroller.cpp
    src/json.cpp
)

set(UNITTEST_SOURCES
    tests/unittest.cpp
    tests/mempool.cpp
    tests/string.cpp
    tests/shareddata.cpp
#    tests/ciepoint.cpp
#    tests/image.cpp
    tests/point.cpp
    tests/uuid.cpp
    tests/timecode.cpp
    tests/numname.cpp
    tests/audio.cpp
    tests/objectbase.cpp
    tests/objectbase2.cpp
)

add_subdirectory(thirdparty)

# External Projects ###########################################################
# As a matter of course, we install all external:
# libraries (both shared and static) in ${CMAKE_BINARY_DIR}/lib
# applications in ${CMAKE_BINARY_DIR}/bin
# includes in ${CMAKE_BINARY_DIR}/include

ExternalProject_Add(
    poco
    URL https://github.com/pocoproject/poco/archive/refs/tags/poco-1.13.3-release.tar.gz
    SOURCE_DIR ${CMAKE_BINARY_DIR}/poco
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}
)
include_directories(${CMAKE_BINARY_DIR}/include)
link_directories(${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin CACHE STRING "Runtime directory")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib CACHE STRING "Library directory")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib CACHE STRING "Archive directory")

# The Promeki Library Build ###################################################
add_library(${APP_NAME} SHARED ${PROMEKI_SOURCES})
set_target_properties(${APP_NAME} PROPERTIES
    VERSION ${APP_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
target_link_libraries(${APP_NAME} PUBLIC stdc++fs)
target_link_libraries(${APP_NAME} PUBLIC PocoFoundation)
target_link_libraries(${APP_NAME} PUBLIC PocoNet)
target_link_libraries(${APP_NAME} PUBLIC PocoJSON)
target_link_libraries(${APP_NAME} PRIVATE nlohmann_json::nlohmann_json) ## FIXME: Will remove as we've got PocoJSON now.
target_compile_definitions(${APP_NAME} PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)
add_dependencies(${APP_NAME} poco libjpeg-turbo-build)

if(PNG_FOUND)
        target_link_libraries(${APP_NAME} PUBLIC png)
        target_include_directories(${APP_NAME} PRIVATE ${PNG_INCLUDE_DIRS})
        target_compile_definitions(${APP_NAME} PRIVATE PROMEKI_ENABLE_PNG)
        target_sources(${APP_NAME} PRIVATE src/imagefileio_png.cpp)
endif()

if(FREETYPE_FOUND)
        target_link_libraries(${APP_NAME} PUBLIC freetype)
        target_include_directories(${APP_NAME} PRIVATE ${FREETYPE_INCLUDE_DIRS})
        target_compile_definitions(${APP_NAME} PRIVATE PROMEKI_ENABLE_FREETYPE)
        target_sources(${APP_NAME} PRIVATE src/fontpainter.cpp)
        #target_sources(unittest PRIVATE tests/image.cpp)
endif()

target_link_libraries(${APP_NAME} PUBLIC sndfile)
target_include_directories(${APP_NAME} PRIVATE ${SNDFILE_INCLUDE_DIRS})
target_compile_definitions(${APP_NAME} PRIVATE PROMEKI_ENABLE_SNDFILE)
target_sources(${APP_NAME} PRIVATE src/audiofile_libsndfile.cpp)

add_executable(unittest ${UNITTEST_SOURCES})
target_compile_definitions(unittest PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)
target_link_libraries(unittest PRIVATE ${APP_NAME})
add_test(NAME unittest COMMAND unittest)

# Find Doxygen package (optional)
find_package(Doxygen)

# Check if Doxygen is installed
if(DOXYGEN_FOUND)

        # Set input and output files
        set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
        set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

        # Configure the Doxyfile for the current build
        configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

        # Add a custom target to run Doxygen whenever the project is built
        add_custom_target(docs 
                        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
                        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                        COMMENT "Generating API documentation with Doxygen"
                        VERBATIM)

else(DOXYGEN_FOUND)
        message("Doxygen needs to be installed to generate the documentation")
endif(DOXYGEN_FOUND)

