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

project(${APP_NAME} VERSION ${APP_VERSION} LANGUAGES C CXX)
include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Build options
option(PROMEKI_BUILD_PROAV "Build the promeki-proav library" ON)
option(PROMEKI_BUILD_TESTS "Build unit tests" ON)
option(PROMEKI_BUILD_DOCS  "Build documentation" OFF)

find_package(Git QUIET REQUIRED)

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

# Output directories ###########################################################

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin CACHE STRING "Runtime directory")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib CACHE STRING "Library directory")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib CACHE STRING "Archive directory")

# Third Party Dependencies ####################################################

# nlohmann/json (header-only, bundled)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
set(JSON_SystemInclude ON CACHE INTERNAL "")
add_subdirectory(thirdparty/nlohmann-json EXCLUDE_FROM_ALL)

# libvtc (video timecode library, static)
set(VTC_BUILD_SHARED OFF CACHE INTERNAL "")
set(VTC_BUILD_STATIC ON CACHE INTERNAL "")
set(VTC_BUILD_UTILS OFF CACHE INTERNAL "")
set(VTC_BUILD_TESTS OFF CACHE INTERNAL "")
set(VTC_BUILD_GUI OFF CACHE INTERNAL "")
set(VTC_BUILD_CPP OFF CACHE INTERNAL "")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory(thirdparty/libvtc EXCLUDE_FROM_ALL)

# Core Library #################################################################

set(PROMEKI_CORE_HEADERS
    include/promeki/ansistream.h
    include/promeki/application.h
    include/promeki/array.h
    include/promeki/buildinfo.h
    include/promeki/buffer.h
    include/promeki/cmdlineparser.h
    include/promeki/datetime.h
    include/promeki/error.h
    include/promeki/file.h
    include/promeki/fileinfo.h
    include/promeki/fourcc.h
    include/promeki/framerate.h
    include/promeki/json.h
    include/promeki/line.h
    include/promeki/list.h
    include/promeki/logger.h
    include/promeki/matrix.h
    include/promeki/matrix3x3.h
    include/promeki/mempool.h
    include/promeki/memspace.h
    include/promeki/metadata.h
    include/promeki/namespace.h
    include/promeki/numname.h
    include/promeki/numnameseq.h
    include/promeki/objectbase.h
    include/promeki/pidcontroller.h
    include/promeki/point.h
    include/promeki/rational.h
    include/promeki/regex.h
    include/promeki/sharedptr.h
    include/promeki/signal.h
    include/promeki/size2d.h
    include/promeki/slot.h
    include/promeki/streamstring.h
    include/promeki/string.h
    include/promeki/stringlist.h
    include/promeki/structdatabase.h
    include/promeki/system.h
    include/promeki/timecode.h
    include/promeki/timestamp.h
    include/promeki/queue.h
    include/promeki/util.h
    include/promeki/uuid.h
    include/promeki/variant.h
)

set(PROMEKI_CORE_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/buildinfo.cpp
    src/application.cpp
    src/objectbase.cpp
    src/system.cpp
    src/util.cpp
    src/rational.cpp
    src/fourcc.cpp
    src/array.cpp
    src/matrix.cpp
    src/matrix3x3.cpp
    src/list.cpp
    src/string.cpp
    src/stringlist.cpp
    src/regex.cpp
    src/error.cpp
    src/uuid.cpp
    src/sharedptr.cpp
    src/point.cpp
    src/line.cpp
    src/size2d.cpp
    src/mempool.cpp
    src/memspace.cpp
    src/buffer.cpp
    src/logger.cpp
    src/ansistream.cpp
    src/fileinfo.cpp
    src/datetime.cpp
    src/timestamp.cpp
    src/timecode.cpp
    src/variant.cpp
    src/metadata.cpp
    src/cmdlineparser.cpp
    src/file.cpp
    src/numname.cpp
    src/numnameseq.cpp
    src/signal.cpp
    src/slot.cpp
    src/pidcontroller.cpp
    src/json.cpp
    src/framerate.cpp
)

add_library(promeki-core SHARED ${PROMEKI_CORE_SOURCES})
add_library(promeki::core ALIAS promeki-core)
set_target_properties(promeki-core PROPERTIES
    VERSION ${APP_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME promeki-core
    EXPORT_NAME core
)
# Create a build-time directory layout so that
# #include <promeki/thirdparty/nlohmann/json.hpp> resolves correctly.
# At install time, the nlohmann headers land under the same path.
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/thirdparty-include/promeki/thirdparty)
file(CREATE_LINK
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann-json/include/nlohmann
    ${CMAKE_BINARY_DIR}/thirdparty-include/promeki/thirdparty/nlohmann
    SYMBOLIC
)
file(CREATE_LINK
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/doctest
    ${CMAKE_BINARY_DIR}/thirdparty-include/promeki/thirdparty/doctest
    SYMBOLIC
)
target_include_directories(promeki-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/thirdparty-include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
# nlohmann headers internally use #include <nlohmann/...> with angle
# brackets, so the parent directory must also be on the include path.
target_include_directories(promeki-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann-json/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty>
)
target_include_directories(promeki-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libvtc/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty>
)
target_link_libraries(promeki-core PUBLIC stdc++fs)
target_link_libraries(promeki-core PRIVATE vtc_static m)
target_compile_definitions(promeki-core PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)

# ProAV Library ################################################################

if(PROMEKI_BUILD_PROAV)
    include(ExternalProject)

    # All thirdparty static libs are built with -fPIC and absorbed into
    # promeki-proav.so.  Their headers install under promeki/thirdparty/
    # so they never collide with system versions of the same libraries.
    set(PROMEKI_THIRDPARTY_PREFIX ${CMAKE_BINARY_DIR}/thirdparty-install)
    set(PROMEKI_THIRDPARTY_INCDIR include/promeki/thirdparty)

    # Pre-create the thirdparty include directory.  ExternalProject builds
    # that export cmake configs (e.g. zlib) reference this path in their
    # INTERFACE_INCLUDE_DIRECTORIES, and CMake validates it exists at
    # configure time of dependent EPs.
    file(MAKE_DIRECTORY ${PROMEKI_THIRDPARTY_PREFIX}/${PROMEKI_THIRDPARTY_INCDIR})

    # zlib — required by libpng and freetype, built first
    # Note: zlib's cmake modifies its source tree (renames zconf.h), so we
    # copy the source to a build-time directory to keep the submodule clean.
    set(PROMEKI_ZLIB_SRC_COPY ${CMAKE_CURRENT_BINARY_DIR}/thirdparty-src/zlib)
    ExternalProject_Add(
        zlib-ep
        DOWNLOAD_COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/zlib
            ${PROMEKI_ZLIB_SRC_COPY}
        SOURCE_DIR ${PROMEKI_ZLIB_SRC_COPY}
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DCMAKE_INSTALL_INCLUDEDIR=${PROMEKI_THIRDPARTY_INCDIR}
            -DZLIB_BUILD_EXAMPLES=OFF
    )

    # libjpeg-turbo
    ExternalProject_Add(
        libjpeg-turbo-ep
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libjpeg-turbo
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DENABLE_SHARED=OFF
            -DENABLE_STATIC=ON
            -DWITH_TURBOJPEG=OFF
            -DCMAKE_INSTALL_INCLUDEDIR=${PROMEKI_THIRDPARTY_INCDIR}
    )

    # libsndfile
    ExternalProject_Add(
        libsndfile-ep
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libsndfile
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DBUILD_SHARED_LIBS=OFF
            -DBUILD_PROGRAMS=OFF
            -DBUILD_EXAMPLES=OFF
            -DBUILD_TESTING=OFF
            -DENABLE_EXTERNAL_LIBS=OFF
            -DENABLE_MPEG=OFF
            -DCMAKE_INSTALL_INCLUDEDIR=${PROMEKI_THIRDPARTY_INCDIR}
    )

    # Paths for finding bundled zlib from libpng/freetype
    set(PROMEKI_ZLIB_INCLUDE ${PROMEKI_THIRDPARTY_PREFIX}/${PROMEKI_THIRDPARTY_INCDIR})
    set(PROMEKI_ZLIB_LIBRARY ${PROMEKI_THIRDPARTY_PREFIX}/lib/libz.a)

    # libpng — depends on zlib
    ExternalProject_Add(
        libpng-ep
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libpng
        DEPENDS zlib-ep
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DZLIB_INCLUDE_DIR=${PROMEKI_ZLIB_INCLUDE}
            -DZLIB_LIBRARY=${PROMEKI_ZLIB_LIBRARY}
            -DPNG_SHARED=OFF
            -DPNG_STATIC=ON
            -DPNG_TESTS=OFF
            -DPNG_TOOLS=OFF
            -DCMAKE_INSTALL_INCLUDEDIR=${PROMEKI_THIRDPARTY_INCDIR}
    )

    # freetype — depends on zlib, disable all other optional deps
    ExternalProject_Add(
        freetype-ep
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/freetype
        DEPENDS zlib-ep
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DZLIB_INCLUDE_DIR=${PROMEKI_ZLIB_INCLUDE}
            -DZLIB_LIBRARY=${PROMEKI_ZLIB_LIBRARY}
            -DBUILD_SHARED_LIBS=OFF
            -DFT_DISABLE_BZIP2=ON
            -DFT_DISABLE_BROTLI=ON
            -DFT_DISABLE_HARFBUZZ=ON
            -DFT_DISABLE_PNG=ON
            -DCMAKE_INSTALL_INCLUDEDIR=${PROMEKI_THIRDPARTY_INCDIR}
    )

    set(PROMEKI_PROAV_HEADERS
        include/promeki/audio.h
        include/promeki/audioblock.h
        include/promeki/audiodesc.h
        include/promeki/audiofile.h
        include/promeki/audiofilefactory.h
        include/promeki/audiogen.h
        include/promeki/ciepoint.h
        include/promeki/ciewavelengthtable.h
        include/promeki/codec.h
        include/promeki/colorspace.h
        include/promeki/colorspaceconverter.h
        include/promeki/fontpainter.h
        include/promeki/frame.h
        include/promeki/image.h
        include/promeki/imagedesc.h
        include/promeki/imagefile.h
        include/promeki/imagefileio.h
        include/promeki/paintengine.h
        include/promeki/pixelformat.h
        include/promeki/pixelformat_old.h
        include/promeki/videodesc.h
        include/promeki/xyzcolor.h
    )

    set(PROMEKI_PROAV_SOURCES
        src/audiodesc.cpp
        src/audio.cpp
        src/audioblock.cpp
        src/audiofile.cpp
        src/audiofilefactory.cpp
        src/audiogen.cpp
        src/imagedesc.cpp
        src/image.cpp
        src/imagefile.cpp
        src/imagefileio.cpp
        src/pixelformat.cpp
        src/pixelformat_rgb8.cpp
        src/pixelformat_rgba8.cpp
        src/pixelformat_jpeg.cpp
        src/paintengine.cpp
        src/colorspaceconverter.cpp
        src/xyzcolor.cpp
        src/ciepoint.cpp
        src/frame.cpp
        src/codec.cpp
        src/videodesc.cpp
    )

    add_library(promeki-proav SHARED ${PROMEKI_PROAV_SOURCES})
    add_library(promeki::proav ALIAS promeki-proav)
    set_target_properties(promeki-proav PROPERTIES
        VERSION ${APP_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        OUTPUT_NAME promeki-proav
        EXPORT_NAME proav
    )
    # Thirdparty headers live under promeki/thirdparty/ to avoid collisions
    # with system versions.  Include paths are set so that:
    #   #include <promeki/thirdparty/sndfile.h>
    #   #include <promeki/thirdparty/jpeglib.h>
    #   #include <promeki/thirdparty/freetype2/ft2build.h>
    # work at both build time and after install.
    #
    # The freetype2/ path is needed because ft2build.h internally does
    # #include <freetype/config/ftheader.h> with angle brackets.
    target_include_directories(promeki-proav PUBLIC
        $<BUILD_INTERFACE:${PROMEKI_THIRDPARTY_PREFIX}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_include_directories(promeki-proav PUBLIC
        $<BUILD_INTERFACE:${PROMEKI_THIRDPARTY_PREFIX}/${PROMEKI_THIRDPARTY_INCDIR}/freetype2>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty/freetype2>
    )
    target_link_directories(promeki-proav PRIVATE
        ${PROMEKI_THIRDPARTY_PREFIX}/lib
        ${PROMEKI_THIRDPARTY_PREFIX}/lib64
    )
    target_link_libraries(promeki-proav PUBLIC promeki-core)
    target_compile_definitions(promeki-proav PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)
    add_dependencies(promeki-proav
        zlib-ep libjpeg-turbo-ep libsndfile-ep libpng-ep freetype-ep
    )

    # All static libs are absorbed into libpromeki-proav.so — consumers
    # never need to link against them directly.
    target_link_libraries(promeki-proav PRIVATE jpeg)
    target_link_libraries(promeki-proav PRIVATE sndfile)
    target_link_libraries(promeki-proav PRIVATE png z)
    target_link_libraries(promeki-proav PRIVATE freetype)
    target_compile_definitions(promeki-proav PRIVATE
        PROMEKI_ENABLE_PNG
        PROMEKI_ENABLE_FREETYPE
        PROMEKI_ENABLE_SNDFILE
    )
    target_sources(promeki-proav PRIVATE
        src/imagefileio_png.cpp
        src/fontpainter.cpp
        src/audiofile_libsndfile.cpp
    )
endif()

# Install ######################################################################

# License and attribution notices
install(FILES
    LICENSE
    THIRD-PARTY-LICENSES
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

# Core library and headers
install(TARGETS promeki-core
    EXPORT promeki-core-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(FILES ${PROMEKI_CORE_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann-json/include/nlohmann
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/doctest
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty
)

# Export core targets
install(EXPORT promeki-core-targets
    FILE promeki-core-targets.cmake
    NAMESPACE promeki::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/promeki
)

# ProAV library and headers
if(PROMEKI_BUILD_PROAV)
    install(TARGETS promeki-proav
        EXPORT promeki-proav-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(FILES ${PROMEKI_PROAV_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki
    )
    # Ship bundled thirdparty headers under promeki/thirdparty/ so they
    # never collide with system versions.  The actual libraries are
    # statically linked into libpromeki-proav.so — no extra -l flags needed.
    install(DIRECTORY ${PROMEKI_THIRDPARTY_PREFIX}/include/promeki/thirdparty
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki
    )
    install(EXPORT promeki-proav-targets
        FILE promeki-proav-targets.cmake
        NAMESPACE promeki::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/promeki
    )
endif()

# Package config files
configure_package_config_file(
    cmake/promekiConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/promekiConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/promeki
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/promekiConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/promekiConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/promekiConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/promeki
)

# Unit Tests ###################################################################

if(PROMEKI_BUILD_TESTS)
    set(UNITTEST_SOURCES
        tests/doctest_main.cpp
        tests/mempool.cpp
        tests/string.cpp
        tests/point.cpp
        tests/uuid.cpp
        tests/timecode.cpp
        tests/numname.cpp
        tests/objectbase.cpp
        tests/objectbase2.cpp
        tests/sharedptr.cpp
        tests/metadata.cpp
        tests/json.cpp
        tests/variant.cpp
        tests/buffer.cpp
        tests/list.cpp
        tests/logger.cpp
        tests/queue.cpp
        tests/streamstring.cpp
    )

    add_executable(unittest-core ${UNITTEST_SOURCES})
    target_compile_definitions(unittest-core PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)
    target_include_directories(unittest-core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
    target_link_libraries(unittest-core PRIVATE promeki-core)
    add_test(NAME unittest-core COMMAND unittest-core)

    if(PROMEKI_BUILD_PROAV)
        set(UNITTEST_PROAV_SOURCES
            tests/doctest_main.cpp
            tests/audio.cpp
            tests/imagedesc.cpp
            tests/audiodesc.cpp
            tests/videodesc.cpp
            tests/image2.cpp
        )
        add_executable(unittest-proav ${UNITTEST_PROAV_SOURCES})
        target_compile_definitions(unittest-proav PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)
        target_include_directories(unittest-proav PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
        target_link_libraries(unittest-proav PRIVATE promeki-proav)
        add_test(NAME unittest-proav COMMAND unittest-proav)
    endif()

    # Run tests as part of the normal build
    add_custom_target(run-tests ALL
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS unittest-core $<$<BOOL:${PROMEKI_BUILD_PROAV}>:unittest-proav>
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running unit tests"
    )
endif()

# Documentation ################################################################

if(PROMEKI_BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
        set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
        configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
        add_custom_target(docs
            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM)
    else()
        message("Doxygen needs to be installed to generate the documentation")
    endif()
endif()
