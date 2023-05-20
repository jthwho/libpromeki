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

project(${APP_NAME} VERSION ${APP_VERSION})
include(CTest)

find_package(Git QUIET REQUIRED)
find_package(PNG)
find_package(Freetype)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE res
    OUTPUT_VARIABLE BUILD_INFO_REPOIDENT
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set_property(GLOBAL APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/.git/index"
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
    invalidate_build_info 
    ALL
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_SOURCE_DIR}/src/buildinfo.cpp.in
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(PROMEKI_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/buildinfo.cpp
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
    src/imagedesc.cpp
    src/image.cpp
    src/audiodesc.cpp
    src/audio.cpp
    src/audiofilefactory.cpp
    src/audiofile.cpp
    src/audiogen.cpp
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
)

set(UNITTEST_SOURCES
    tests/unittest.cpp
    tests/mempool.cpp
    tests/string.cpp
    tests/shareddata.cpp
    tests/image.cpp
#    tests/ciepoint.cpp
    tests/point.cpp
    tests/uuid.cpp
    tests/timecode.cpp
    tests/numname.cpp
    tests/audio.cpp
    tests/objectbase.cpp
)

add_library(${APP_NAME} SHARED ${PROMEKI_SOURCES})
target_link_libraries(${APP_NAME} PUBLIC stdc++fs)
target_compile_definitions(${APP_NAME} PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)

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
endif()

target_link_libraries(${APP_NAME} PUBLIC sndfile)
target_include_directories(${APP_NAME} PRIVATE ${SNDFILE_INCLUDE_DIRS})
target_compile_definitions(${APP_NAME} PRIVATE PROMEKI_ENABLE_SNDFILE)
target_sources(${APP_NAME} PRIVATE src/audiofile_libsndfile.cpp)

add_executable(unittest ${UNITTEST_SOURCES})
target_compile_definitions(unittest PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)
target_link_libraries(unittest PRIVATE ${APP_NAME})
add_test(NAME unittest COMMAND unittest)
 
