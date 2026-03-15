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
option(PROMEKI_BUILD_PROAV  "Build the promeki-proav library"  ON)
option(PROMEKI_BUILD_MUSIC  "Build the promeki-music library"  ON)
option(PROMEKI_BUILD_TUI    "Build the promeki-tui library"    ON)
option(PROMEKI_BUILD_UTILS  "Build utility applications"       ON)
option(PROMEKI_BUILD_DEMOS  "Build demonstration applications" ON)
option(PROMEKI_BUILD_TESTS  "Build unit tests"                 ON)
option(PROMEKI_BUILD_DOCS   "Build documentation"              OFF)

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
    include/promeki/core/ansistream.h
    include/promeki/core/application.h
    include/promeki/core/array.h
    include/promeki/core/buildinfo.h
    include/promeki/core/buffer.h
    include/promeki/core/bytearray.h
    include/promeki/core/char.h
    include/promeki/core/cmdlineparser.h
    include/promeki/core/datetime.h
    include/promeki/core/env.h
    include/promeki/core/error.h
    include/promeki/core/event.h
    include/promeki/core/eventloop.h
    include/promeki/core/file.h
    include/promeki/core/fileinfo.h
    include/promeki/core/fnv1a.h
    include/promeki/core/fourcc.h
    include/promeki/core/framerate.h
    include/promeki/core/json.h
    include/promeki/core/line.h
    include/promeki/core/list.h
    include/promeki/core/logger.h
    include/promeki/core/map.h
    include/promeki/core/matrix.h
    include/promeki/core/matrix3x3.h
    include/promeki/core/md5.h
    include/promeki/core/mempool.h
    include/promeki/core/memspace.h
    include/promeki/core/metadata.h
    include/promeki/core/namespace.h
    include/promeki/core/numname.h
    include/promeki/core/obfuscatedstring.h
    include/promeki/core/numnameseq.h
    include/promeki/core/objectbase.h
    include/promeki/core/pidcontroller.h
    include/promeki/core/platform.h
    include/promeki/core/point.h
    include/promeki/core/rational.h
    include/promeki/core/regex.h
    include/promeki/core/securemem.h
    include/promeki/core/set.h
    include/promeki/core/sha1.h
    include/promeki/core/sharedptr.h
    include/promeki/core/signal.h
    include/promeki/core/size2d.h
    include/promeki/core/thread.h
    include/promeki/core/timerevent.h
    include/promeki/core/slot.h
    include/promeki/core/streamstring.h
    include/promeki/core/string.h
    include/promeki/core/stringdata.h
    include/promeki/core/stringlist.h
    include/promeki/core/structdatabase.h
    include/promeki/core/system.h
    include/promeki/core/timecode.h
    include/promeki/core/timestamp.h
    include/promeki/core/queue.h
    include/promeki/core/util.h
    include/promeki/core/uuid.h
    include/promeki/core/variant.h
    include/promeki/core/rect.h
    include/promeki/core/color.h
    include/promeki/core/terminal.h
    include/promeki/core/keyevent.h
    include/promeki/core/mouseevent.h
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
    src/map.cpp
    src/matrix3x3.cpp
    src/list.cpp
    src/char.cpp
    src/string.cpp
    src/stringdata.cpp
    src/stringlist.cpp
    src/set.cpp
    src/regex.cpp
    src/event.cpp
    src/eventloop.cpp
    src/env.cpp
    src/error.cpp
    src/uuid.cpp
    src/md5.cpp
    src/sha1.cpp
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
    src/thread.cpp
    src/pidcontroller.cpp
    src/json.cpp
    src/framerate.cpp
    src/rect.cpp
    src/color.cpp
    src/terminal.cpp
    src/keyevent.cpp
    src/mouseevent.cpp
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
        include/promeki/proav/audio.h
        include/promeki/proav/audioblock.h
        include/promeki/proav/audiodesc.h
        include/promeki/proav/audiofile.h
        include/promeki/proav/audiofilefactory.h
        include/promeki/proav/audiogen.h
        include/promeki/proav/ciepoint.h
        include/promeki/proav/ciewavelengthtable.h
        include/promeki/proav/codec.h
        include/promeki/proav/colorspace.h
        include/promeki/proav/colorspaceconverter.h
        include/promeki/proav/fontpainter.h
        include/promeki/proav/frame.h
        include/promeki/proav/image.h
        include/promeki/proav/imagedesc.h
        include/promeki/proav/imagefile.h
        include/promeki/proav/imagefileio.h
        include/promeki/proav/paintengine.h
        include/promeki/proav/pixelformat.h
        include/promeki/proav/pixelformat_old.h
        include/promeki/proav/videodesc.h
        include/promeki/proav/xyzcolor.h
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
        src/colorspace.cpp
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

# Music Library ################################################################

if(PROMEKI_BUILD_MUSIC)
    if(NOT PROMEKI_BUILD_PROAV)
        message(FATAL_ERROR "promeki-music requires promeki-proav (PROMEKI_BUILD_PROAV=ON)")
    endif()

    set(PROMEKI_MUSIC_HEADERS
        include/promeki/music/musicalnote.h
        include/promeki/music/musicalscale.h
        include/promeki/music/midinote.h
        include/promeki/music/midinotenames.h
        include/promeki/music/notesequenceparser.h
    )

    set(PROMEKI_MUSIC_SOURCES
        src/musicalscale.cpp
        src/midinote.cpp
        src/midinotenames.cpp
        src/notesequenceparser.cpp
    )

    add_library(promeki-music SHARED ${PROMEKI_MUSIC_SOURCES})
    add_library(promeki::music ALIAS promeki-music)
    set_target_properties(promeki-music PROPERTIES
        VERSION ${APP_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        OUTPUT_NAME promeki-music
        EXPORT_NAME music
    )
    target_include_directories(promeki-music PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(promeki-music PUBLIC promeki-proav)
    target_compile_definitions(promeki-music PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)
endif()

# TUI Library ##################################################################

if(PROMEKI_BUILD_TUI)
    set(PROMEKI_TUI_HEADERS
        include/promeki/tui/style.h
        include/promeki/tui/screen.h
        include/promeki/tui/widget.h
        include/promeki/tui/painter.h
        include/promeki/tui/layout.h
        include/promeki/tui/application.h
        include/promeki/tui/label.h
        include/promeki/tui/button.h
        include/promeki/tui/checkbox.h
        include/promeki/tui/lineedit.h
        include/promeki/tui/textarea.h
        include/promeki/tui/listview.h
        include/promeki/tui/frame.h
        include/promeki/tui/scrollarea.h
        include/promeki/tui/splitter.h
        include/promeki/tui/tabwidget.h
        include/promeki/tui/progressbar.h
        include/promeki/tui/statusbar.h
        include/promeki/tui/menu.h
        include/promeki/tui/palette.h
        include/promeki/tui/inputparser.h
    )

    set(PROMEKI_TUI_SOURCES
        src/tui/style.cpp
        src/tui/palette.cpp
        src/tui/screen.cpp
        src/tui/widget.cpp
        src/tui/painter.cpp
        src/tui/layout.cpp
        src/tui/application.cpp
        src/tui/label.cpp
        src/tui/button.cpp
        src/tui/checkbox.cpp
        src/tui/lineedit.cpp
        src/tui/textarea.cpp
        src/tui/listview.cpp
        src/tui/frame.cpp
        src/tui/scrollarea.cpp
        src/tui/splitter.cpp
        src/tui/tabwidget.cpp
        src/tui/progressbar.cpp
        src/tui/statusbar.cpp
        src/tui/menu.cpp
        src/tui/inputparser.cpp
    )

    add_library(promeki-tui SHARED ${PROMEKI_TUI_SOURCES})
    add_library(promeki::tui ALIAS promeki-tui)
    set_target_properties(promeki-tui PROPERTIES
        VERSION ${APP_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        OUTPUT_NAME promeki-tui
        EXPORT_NAME tui
    )
    target_include_directories(promeki-tui PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(promeki-tui PUBLIC promeki-core)
    target_compile_definitions(promeki-tui PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)
endif()

# Utility Applications #########################################################

if(PROMEKI_BUILD_UTILS)
    add_subdirectory(utils)
endif()

# Demonstration Applications ###################################################

if(PROMEKI_BUILD_DEMOS)
    add_subdirectory(demos)
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
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki/core
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
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki/proav
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

# TUI library and headers
if(PROMEKI_BUILD_TUI)
    install(TARGETS promeki-tui
        EXPORT promeki-tui-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(FILES ${PROMEKI_TUI_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki/tui
    )
    install(EXPORT promeki-tui-targets
        FILE promeki-tui-targets.cmake
        NAMESPACE promeki::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/promeki
    )
endif()

# Music library and headers
if(PROMEKI_BUILD_MUSIC)
    install(TARGETS promeki-music
        EXPORT promeki-music-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(FILES ${PROMEKI_MUSIC_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki/music
    )
    install(EXPORT promeki-music-targets
        FILE promeki-music-targets.cmake
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
        tests/bytearray.cpp
        tests/md5.cpp
        tests/sha1.cpp
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
        tests/rational.cpp
        tests/map.cpp
        tests/error.cpp
        tests/fourcc.cpp
        tests/size2d.cpp
        tests/line.cpp
        tests/array.cpp
        tests/stringlist.cpp
        tests/framerate.cpp
        tests/datetime.cpp
        tests/timestamp.cpp
        tests/matrix.cpp
        tests/matrix3x3.cpp
        tests/regex.cpp
        tests/application.cpp
        tests/system.cpp
        tests/memspace.cpp
        tests/file.cpp
        tests/buildinfo.cpp
        tests/pidcontroller.cpp
        tests/set.cpp
        tests/structdatabase.cpp
        tests/cmdlineparser.cpp
        tests/ansistream.cpp
        tests/fileinfo.cpp
        tests/fnv1a.cpp
        tests/obfuscatedstring.cpp
        tests/signal.cpp
        tests/event.cpp
        tests/eventloop.cpp
        tests/thread.cpp
        tests/securebuffer.cpp
        tests/rect.cpp
        tests/color.cpp
        tests/env.cpp
        tests/terminal.cpp
        tests/pair.cpp
        tests/result.cpp
        tests/hashmap.cpp
        tests/hashset.cpp
        tests/deque.cpp
        tests/stack.cpp
        tests/priorityqueue.cpp
        tests/span.cpp
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
            tests/pixelformat.cpp
            tests/frame.cpp
            tests/colorspace.cpp
            tests/xyzcolor.cpp
            tests/imagefileio.cpp
            tests/codec.cpp
            tests/colorspaceconverter.cpp
            tests/ciewavelengthtable.cpp
            tests/audioblock.cpp
            tests/audiofilefactory.cpp
            tests/ciepoint.cpp
            tests/imagefile.cpp
            tests/paintengine.cpp
        )
        add_executable(unittest-proav ${UNITTEST_PROAV_SOURCES})
        target_compile_definitions(unittest-proav PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)
        target_include_directories(unittest-proav PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
        target_link_libraries(unittest-proav PRIVATE promeki-proav)
        add_test(NAME unittest-proav COMMAND unittest-proav)
    endif()

    if(PROMEKI_BUILD_TUI)
        set(UNITTEST_TUI_SOURCES
            tests/doctest_main.cpp
            tests/tui/screen.cpp
            tests/tui/widget.cpp
            tests/tui/painter.cpp
            tests/tui/layout.cpp
            tests/tui/label.cpp
            tests/tui/button.cpp
            tests/tui/checkbox.cpp
            tests/tui/frame.cpp
            tests/tui/inputparser.cpp
            tests/tui/palette.cpp
            tests/tui/lineedit.cpp
            tests/tui/textarea.cpp
            tests/tui/listview.cpp
            tests/tui/scrollarea.cpp
            tests/tui/splitter.cpp
            tests/tui/tabwidget.cpp
            tests/tui/progressbar.cpp
            tests/tui/statusbar.cpp
            tests/tui/menu.cpp
        )
        add_executable(unittest-tui ${UNITTEST_TUI_SOURCES})
        target_compile_definitions(unittest-tui PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)
        target_include_directories(unittest-tui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
        target_link_libraries(unittest-tui PRIVATE promeki-tui)
        add_test(NAME unittest-tui COMMAND unittest-tui)
    endif()

    if(PROMEKI_BUILD_MUSIC)
        set(UNITTEST_MUSIC_SOURCES
            tests/doctest_main.cpp
            tests/midinote.cpp
            tests/midinotenames.cpp
            tests/musicalscale.cpp
            tests/notesequenceparser.cpp
            tests/musicalnote.cpp
        )
        add_executable(unittest-music ${UNITTEST_MUSIC_SOURCES})
        target_compile_definitions(unittest-music PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)
        target_include_directories(unittest-music PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
        target_link_libraries(unittest-music PRIVATE promeki-music)
        add_test(NAME unittest-music COMMAND unittest-music)
    endif()

    # Run tests as part of the normal build
    add_custom_target(run-tests ALL
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS unittest-core
            $<$<BOOL:${PROMEKI_BUILD_PROAV}>:unittest-proav>
            $<$<BOOL:${PROMEKI_BUILD_TUI}>:unittest-tui>
            $<$<BOOL:${PROMEKI_BUILD_MUSIC}>:unittest-music>
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running unit tests"
    )
endif()

# Custom Clean Targets #########################################################
# libclean:  removes only promeki library objects, tests, utils, and demos
#            (preserves third-party builds so they don't need to be rebuilt)
# clean:     built-in CMake target that removes everything including third-party

set(PROMEKI_CLEAN_DIRS
    ${CMAKE_BINARY_DIR}/CMakeFiles/promeki-core.dir
    ${CMAKE_BINARY_DIR}/CMakeFiles/libpromeki_invalidate_build_info.dir
    ${CMAKE_BINARY_DIR}/buildinfo.cpp
    ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libpromeki-core*
)

set(PROMEKI_CLEAN_TEST_DIRS "")
if(PROMEKI_BUILD_TESTS)
    list(APPEND PROMEKI_CLEAN_DIRS
        ${CMAKE_BINARY_DIR}/CMakeFiles/unittest-core.dir
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unittest-core
        ${CMAKE_BINARY_DIR}/CMakeFiles/run-tests.dir
    )
endif()

if(PROMEKI_BUILD_PROAV)
    list(APPEND PROMEKI_CLEAN_DIRS
        ${CMAKE_BINARY_DIR}/CMakeFiles/promeki-proav.dir
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libpromeki-proav*
    )
    if(PROMEKI_BUILD_TESTS)
        list(APPEND PROMEKI_CLEAN_DIRS
            ${CMAKE_BINARY_DIR}/CMakeFiles/unittest-proav.dir
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unittest-proav
        )
    endif()
endif()

if(PROMEKI_BUILD_TUI)
    list(APPEND PROMEKI_CLEAN_DIRS
        ${CMAKE_BINARY_DIR}/CMakeFiles/promeki-tui.dir
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libpromeki-tui*
    )
    if(PROMEKI_BUILD_TESTS)
        list(APPEND PROMEKI_CLEAN_DIRS
            ${CMAKE_BINARY_DIR}/CMakeFiles/unittest-tui.dir
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unittest-tui
        )
    endif()
endif()

if(PROMEKI_BUILD_MUSIC)
    list(APPEND PROMEKI_CLEAN_DIRS
        ${CMAKE_BINARY_DIR}/CMakeFiles/promeki-music.dir
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libpromeki-music*
    )
    if(PROMEKI_BUILD_TESTS)
        list(APPEND PROMEKI_CLEAN_DIRS
            ${CMAKE_BINARY_DIR}/CMakeFiles/unittest-music.dir
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unittest-music
        )
    endif()
endif()

if(PROMEKI_BUILD_UTILS)
    list(APPEND PROMEKI_CLEAN_DIRS
        ${CMAKE_BINARY_DIR}/utils
    )
endif()

if(PROMEKI_BUILD_DEMOS)
    list(APPEND PROMEKI_CLEAN_DIRS
        ${CMAKE_BINARY_DIR}/demos
    )
endif()

add_custom_target(libclean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${PROMEKI_CLEAN_DIRS}
    COMMENT "Cleaning promeki objects (preserving third-party builds)"
)

# Documentation ################################################################

set(PROMEKI_DOCS_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/doxygen" CACHE PATH
    "Output directory for generated documentation")

find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    string(SUBSTRING "${BUILD_INFO_REPOIDENT}" 0 7 BUILD_INFO_REPOIDENT_SHORT)
    set(PROMEKI_DOCS_VERSION "${BUILD_INFO_VERSION}" CACHE STRING
        "Version string shown in generated documentation")
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/docs/footer.html.in
        ${CMAKE_CURRENT_BINARY_DIR}/doxygen_footer.html @ONLY)
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    file(GLOB PROMEKI_DOCS_IMAGES ${CMAKE_CURRENT_SOURCE_DIR}/docs/*.jpg)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PROMEKI_DOCS_OUTPUT_DIR}/html/docs
        COMMAND ${CMAKE_COMMAND} -E copy ${PROMEKI_DOCS_IMAGES} ${PROMEKI_DOCS_OUTPUT_DIR}/html/docs/
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM)
    if(PROMEKI_BUILD_DOCS)
        add_custom_target(docs-all ALL DEPENDS docs)
    endif()
else()
    if(PROMEKI_BUILD_DOCS)
        message(FATAL_ERROR "PROMEKI_BUILD_DOCS is ON but Doxygen was not found")
    endif()
endif()
