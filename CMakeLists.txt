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
include(ExternalProject)

# Build options ################################################################

option(PROMEKI_BUILD_TUI    "Build the promeki-tui library"    ON)
option(PROMEKI_BUILD_SDL    "Build the promeki-sdl library"    OFF)
option(PROMEKI_BUILD_UTILS  "Build utility applications"       ON)
option(PROMEKI_BUILD_DEMOS  "Build demonstration applications" ON)
option(PROMEKI_BUILD_TESTS  "Build unit tests"                 ON)
option(PROMEKI_BUILD_DOCS   "Build documentation"              OFF)

# Feature flags for the main promeki library
option(PROMEKI_ENABLE_NETWORK  "Include networking support"             ON)
option(PROMEKI_ENABLE_PROAV    "Include pro A/V support"                ON)
option(PROMEKI_ENABLE_MUSIC    "Include music support"                  ON)
option(PROMEKI_ENABLE_PNG      "Include PNG image I/O"                  ON)
option(PROMEKI_ENABLE_JPEG     "Include JPEG codec"                     ON)
option(PROMEKI_ENABLE_FREETYPE "Include FreeType font rendering"        ON)
option(PROMEKI_ENABLE_AUDIO    "Include audio file I/O (libsndfile)"    ON)
option(PROMEKI_ENABLE_CSC      "Include color space conversion (Highway SIMD)" ON)

# Vendored vs system dependency options
option(PROMEKI_USE_SYSTEM_ZLIB          "Use system zlib instead of vendored zlib-ng"   OFF)
option(PROMEKI_USE_SYSTEM_LIBSPNG       "Use system libspng instead of vendored"        OFF)
option(PROMEKI_USE_SYSTEM_LIBJPEG       "Use system libjpeg instead of vendored"        OFF)
option(PROMEKI_USE_SYSTEM_FREETYPE      "Use system freetype instead of vendored"       OFF)
option(PROMEKI_USE_SYSTEM_SNDFILE       "Use system libsndfile instead of vendored"     OFF)
option(PROMEKI_USE_SYSTEM_NLOHMANN_JSON "Use system nlohmann_json instead of vendored"  OFF)
option(PROMEKI_USE_SYSTEM_VTC           "Use system libvtc instead of vendored"         OFF)
option(PROMEKI_USE_SYSTEM_HIGHWAY      "Use system Highway instead of vendored"        OFF)

# Dependency validation ########################################################

if(PROMEKI_ENABLE_MUSIC AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_MUSIC requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_PNG AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_PNG requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_JPEG AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_JPEG requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_FREETYPE AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_FREETYPE requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_AUDIO AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_AUDIO requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_CSC AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_CSC requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_BUILD_SDL AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_BUILD_SDL requires PROMEKI_ENABLE_PROAV")
endif()

# Build info ###################################################################

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
configure_file(src/core/buildinfo.cpp.in buildinfo.cpp)

add_custom_target(
    libpromeki_invalidate_build_info
    ALL
    COMMAND ${CMAKE_COMMAND} -E touch ${PROJECT_SOURCE_DIR}/src/core/buildinfo.cpp.in
)

# Generate config.h
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/promeki/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/promeki/config.h
)

# Output directories ###########################################################

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin CACHE STRING "Runtime directory")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib CACHE STRING "Library directory")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib CACHE STRING "Archive directory")

# Third Party Dependencies ####################################################

# Prefix for vendored thirdparty builds
set(PROMEKI_THIRDPARTY_PREFIX ${CMAKE_BINARY_DIR}/thirdparty-install)
file(MAKE_DIRECTORY ${PROMEKI_THIRDPARTY_PREFIX}/include)

# --- nlohmann/json ---
if(PROMEKI_USE_SYSTEM_NLOHMANN_JSON)
    find_package(nlohmann_json REQUIRED)
else()
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install OFF CACHE INTERNAL "")
    set(JSON_SystemInclude ON CACHE INTERNAL "")
    add_subdirectory(thirdparty/nlohmann-json EXCLUDE_FROM_ALL)
endif()

# --- libvtc ---
if(PROMEKI_USE_SYSTEM_VTC)
    find_package(vtc REQUIRED)
    set(PROMEKI_VTC_TARGET vtc::vtc)
else()
    set(VTC_BUILD_SHARED OFF CACHE INTERNAL "")
    set(VTC_BUILD_STATIC ON CACHE INTERNAL "")
    set(VTC_BUILD_UTILS OFF CACHE INTERNAL "")
    set(VTC_BUILD_TESTS OFF CACHE INTERNAL "")
    set(VTC_BUILD_GUI OFF CACHE INTERNAL "")
    set(VTC_BUILD_CPP OFF CACHE INTERNAL "")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_subdirectory(thirdparty/libvtc EXCLUDE_FROM_ALL)
    set(PROMEKI_VTC_TARGET vtc_static)
endif()

# --- Google Highway (SIMD, needed by CSC) ---
if(PROMEKI_ENABLE_CSC)
    if(PROMEKI_USE_SYSTEM_HIGHWAY)
        find_package(HWY REQUIRED)
        set(PROMEKI_HWY_TARGET hwy::hwy)
    else()
        set(HWY_ENABLE_TESTS OFF CACHE INTERNAL "")
        set(HWY_ENABLE_EXAMPLES OFF CACHE INTERNAL "")
        set(HWY_ENABLE_CONTRIB OFF CACHE INTERNAL "")
        set(HWY_ENABLE_INSTALL OFF CACHE INTERNAL "")
        set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
        set(BUILD_SHARED_LIBS OFF)
        add_subdirectory(thirdparty/highway EXCLUDE_FROM_ALL)
        set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})
        set(PROMEKI_HWY_TARGET hwy)
    endif()
endif()

# --- zlib (vendored as zlib-ng in zlib-compat mode; needed by libspng and freetype) ---
# zlib-ng is a drop-in replacement for stock zlib with significantly faster
# deflate/inflate performance on modern x86_64 / ARM. With ZLIB_COMPAT=ON it
# installs as libz.a with the standard zlib.h ABI, so all consumers
# (libspng, freetype, anything else) link against it unchanged.
if(PROMEKI_ENABLE_PNG OR PROMEKI_ENABLE_FREETYPE)
    if(PROMEKI_USE_SYSTEM_ZLIB)
        find_package(ZLIB REQUIRED)
        set(PROMEKI_ZLIB_TARGET ZLIB::ZLIB)
        set(PROMEKI_ZLIB_INCLUDE ${ZLIB_INCLUDE_DIRS})
        set(PROMEKI_ZLIB_LIBRARY ${ZLIB_LIBRARIES})
    else()
        set(PROMEKI_ZLIB_SRC_COPY ${CMAKE_CURRENT_BINARY_DIR}/thirdparty-src/zlib-ng)
        ExternalProject_Add(
            zlib-ng-ep
            DOWNLOAD_COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/zlib-ng
                ${PROMEKI_ZLIB_SRC_COPY}
            SOURCE_DIR ${PROMEKI_ZLIB_SRC_COPY}
            CMAKE_ARGS
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DZLIB_COMPAT=ON
                -DZLIB_ENABLE_TESTS=OFF
                -DZLIBNG_ENABLE_TESTS=OFF
                -DBUILD_SHARED_LIBS=OFF
                -DWITH_GTEST=OFF
        )
        set(PROMEKI_ZLIB_INCLUDE ${PROMEKI_THIRDPARTY_PREFIX}/include)
        set(PROMEKI_ZLIB_LIBRARY ${PROMEKI_THIRDPARTY_PREFIX}/lib/libz.a)
        add_library(promeki_zlib_vendored STATIC IMPORTED)
        set_target_properties(promeki_zlib_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_ZLIB_LIBRARY}
            INTERFACE_INCLUDE_DIRECTORIES ${PROMEKI_ZLIB_INCLUDE}
        )
        add_dependencies(promeki_zlib_vendored zlib-ng-ep)
        set(PROMEKI_ZLIB_TARGET promeki_zlib_vendored)
    endif()
endif()

# --- libjpeg-turbo ---
if(PROMEKI_ENABLE_JPEG)
    if(PROMEKI_USE_SYSTEM_LIBJPEG)
        find_package(JPEG REQUIRED)
        set(PROMEKI_JPEG_TARGET JPEG::JPEG)
    else()
        ExternalProject_Add(
            libjpeg-turbo-ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libjpeg-turbo
            CMAKE_ARGS
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DENABLE_SHARED=OFF
                -DENABLE_STATIC=ON
                -DWITH_TURBOJPEG=OFF
        )
        add_library(promeki_jpeg_vendored STATIC IMPORTED)
        set_target_properties(promeki_jpeg_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libjpeg.a
        )
        add_dependencies(promeki_jpeg_vendored libjpeg-turbo-ep)
        set(PROMEKI_JPEG_TARGET promeki_jpeg_vendored)
    endif()
endif()

# --- libspng ---
# libspng is used in place of libpng. It's a single-file PNG codec with a
# clean stream/buffer API and no setjmp/longjmp, sized for media-pipeline
# usage (image sequences, realtime decode/encode).
if(PROMEKI_ENABLE_PNG)
    if(PROMEKI_USE_SYSTEM_LIBSPNG)
        find_package(SPNG REQUIRED)
        set(PROMEKI_SPNG_TARGET spng::spng)
    else()
        ExternalProject_Add(
            libspng-ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libspng
            DEPENDS zlib-ng-ep
            CMAKE_ARGS
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DCMAKE_PREFIX_PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DZLIB_ROOT=${PROMEKI_THIRDPARTY_PREFIX}
                -DZLIB_INCLUDE_DIR=${PROMEKI_ZLIB_INCLUDE}
                -DZLIB_LIBRARY=${PROMEKI_ZLIB_LIBRARY}
                -DSPNG_SHARED=OFF
                -DSPNG_STATIC=ON
                -DBUILD_EXAMPLES=OFF
        )
        add_library(promeki_spng_vendored STATIC IMPORTED)
        set_target_properties(promeki_spng_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libspng_static.a
            INTERFACE_INCLUDE_DIRECTORIES ${PROMEKI_THIRDPARTY_PREFIX}/include
            INTERFACE_COMPILE_DEFINITIONS SPNG_STATIC
        )
        add_dependencies(promeki_spng_vendored libspng-ep)
        set(PROMEKI_SPNG_TARGET promeki_spng_vendored)
    endif()
endif()

# --- libsndfile ---
if(PROMEKI_ENABLE_AUDIO)
    if(PROMEKI_USE_SYSTEM_SNDFILE)
        find_package(SndFile REQUIRED)
        set(PROMEKI_SNDFILE_TARGET SndFile::sndfile)
    else()
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
        )
        add_library(promeki_sndfile_vendored STATIC IMPORTED)
        set_target_properties(promeki_sndfile_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libsndfile.a
        )
        add_dependencies(promeki_sndfile_vendored libsndfile-ep)
        set(PROMEKI_SNDFILE_TARGET promeki_sndfile_vendored)
    endif()
endif()

# --- freetype ---
if(PROMEKI_ENABLE_FREETYPE)
    if(PROMEKI_USE_SYSTEM_FREETYPE)
        find_package(Freetype REQUIRED)
        set(PROMEKI_FREETYPE_TARGET Freetype::Freetype)
    else()
        ExternalProject_Add(
            freetype-ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/freetype
            DEPENDS zlib-ng-ep
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
        )
        add_library(promeki_freetype_vendored STATIC IMPORTED)
        set_target_properties(promeki_freetype_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libfreetype.a
        )
        add_dependencies(promeki_freetype_vendored freetype-ep)
        set(PROMEKI_FREETYPE_TARGET promeki_freetype_vendored)
    endif()
endif()

# Main Library: promeki ########################################################

set(PROMEKI_HEADERS
    include/promeki/algorithm.h
    include/promeki/ansistream.h
    include/promeki/application.h
    include/promeki/array.h
    include/promeki/atomic.h
    include/promeki/audiolevel.h
    include/promeki/benchmark.h
    include/promeki/buffer.h
    include/promeki/bufferediodevice.h
    include/promeki/bufferiodevice.h
    include/promeki/bufferview.h
    include/promeki/buildinfo.h
    include/promeki/bytearray.h
    include/promeki/char.h
    include/promeki/ciepoint.h
    include/promeki/ciewavelengthtable.h
    include/promeki/cmdlineparser.h
    include/promeki/color.h
    include/promeki/colormodel.h
    include/promeki/configoption.h
    include/promeki/datetime.h
    include/promeki/dir.h
    include/promeki/duration.h
    include/promeki/elapsedtimer.h
    include/promeki/env.h
    include/promeki/error.h
    include/promeki/event.h
    include/promeki/eventloop.h
    include/promeki/file.h
    include/promeki/fileformatfactory.h
    include/promeki/fileinfo.h
    include/promeki/filepath.h
    include/promeki/fnv1a.h
    include/promeki/fourcc.h
    include/promeki/framerate.h
    include/promeki/future.h
    include/promeki/iodevice.h
    include/promeki/json.h
    include/promeki/keyevent.h
    include/promeki/layout.h
    include/promeki/line.h
    include/promeki/list.h
    include/promeki/logger.h
    include/promeki/map.h
    include/promeki/matrix.h
    include/promeki/matrix3x3.h
    include/promeki/md5.h
    include/promeki/mempool.h
    include/promeki/memspace.h
    include/promeki/metadata.h
    include/promeki/mouseevent.h
    include/promeki/mutex.h
    include/promeki/namespace.h
    include/promeki/numname.h
    include/promeki/numnameseq.h
    include/promeki/objectbase.h
    include/promeki/obfuscatedstring.h
    include/promeki/pidcontroller.h
    include/promeki/pixeldesc.h
    include/promeki/pixelformat.h
    include/promeki/platform.h
    include/promeki/point.h
    include/promeki/process.h
    include/promeki/promise.h
    include/promeki/queue.h
    include/promeki/random.h
    include/promeki/rational.h
    include/promeki/readwritelock.h
    include/promeki/rect.h
    include/promeki/regex.h
    include/promeki/securemem.h
    include/promeki/set.h
    include/promeki/sha1.h
    include/promeki/sharedptr.h
    include/promeki/signal.h
    include/promeki/size2d.h
    include/promeki/slot.h
    include/promeki/streamstring.h
    include/promeki/string.h
    include/promeki/stringdata.h
    include/promeki/stringlist.h
    include/promeki/stringregistry.h
    include/promeki/structdatabase.h
    include/promeki/system.h
    include/promeki/terminal.h
    include/promeki/strand.h
    include/promeki/thread.h
    include/promeki/threadpool.h
    include/promeki/timecode.h
    include/promeki/timecodegenerator.h
    include/promeki/timerevent.h
    include/promeki/timestamp.h
    include/promeki/util.h
    include/promeki/uuid.h
    include/promeki/variant.h
    include/promeki/variantdatabase.h
    include/promeki/waitcondition.h
    include/promeki/widget.h
    include/promeki/xyzcolor.h
)

set(PROMEKI_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/buildinfo.cpp
    src/core/ansistream.cpp
    src/core/application.cpp
    src/core/array.cpp
    src/core/audiolevel.cpp
    src/core/benchmark.cpp
    src/core/buffer.cpp
    src/core/bufferediodevice.cpp
    src/core/bufferiodevice.cpp
    src/core/char.cpp
    src/core/ciepoint.cpp
    src/core/cmdlineparser.cpp
    src/core/color.cpp
    src/core/colormodel.cpp
    src/core/datastream.cpp
    src/core/datetime.cpp
    src/core/dir.cpp
    src/core/duration.cpp
    src/core/env.cpp
    src/core/error.cpp
    src/core/event.cpp
    src/core/eventloop.cpp
    src/core/file.cpp
    src/core/fileinfo.cpp
    src/core/fileiodevice.cpp
    src/core/fourcc.cpp
    src/core/framerate.cpp
    src/core/iodevice.cpp
    src/core/json.cpp
    src/core/keyevent.cpp
    src/core/layout.cpp
    src/core/line.cpp
    src/core/list.cpp
    src/core/logger.cpp
    src/core/map.cpp
    src/core/matrix.cpp
    src/core/matrix3x3.cpp
    src/core/md5.cpp
    src/core/mempool.cpp
    src/core/memspace.cpp
    src/core/metadata.cpp
    src/core/mouseevent.cpp
    src/core/numname.cpp
    src/core/numnameseq.cpp
    src/core/objectbase.cpp
    src/core/pidcontroller.cpp
    src/core/pixeldesc.cpp
    src/core/pixelformat.cpp
    src/core/point.cpp
    src/core/process.cpp
    src/core/random.cpp
    src/core/rational.cpp
    src/core/rect.cpp
    src/core/regex.cpp
    src/core/set.cpp
    src/core/sha1.cpp
    src/core/sharedptr.cpp
    src/core/signal.cpp
    src/core/size2d.cpp
    src/core/slot.cpp
    src/core/string.cpp
    src/core/stringdata.cpp
    src/core/stringiodevice.cpp
    src/core/stringlist.cpp
    src/core/system.cpp
    src/core/terminal.cpp
    src/core/textstream.cpp
    src/core/thread.cpp
    src/core/threadpool.cpp
    src/core/timecode.cpp
    src/core/timecodegenerator.cpp
    src/core/timestamp.cpp
    src/core/util.cpp
    src/core/uuid.cpp
    src/core/variant.cpp
    src/core/widget.cpp
    src/core/xyzcolor.cpp
)

# Network sources
if(PROMEKI_ENABLE_NETWORK)
    list(APPEND PROMEKI_HEADERS
        include/promeki/abstractsocket.h
        include/promeki/ipv4address.h
        include/promeki/ipv6address.h
        include/promeki/macaddress.h
        include/promeki/multicastmanager.h
        include/promeki/networkaddress.h
        include/promeki/prioritysocket.h
        include/promeki/rawsocket.h
        include/promeki/rtppacket.h
        include/promeki/rtppayload.h
        include/promeki/rtpsession.h
        include/promeki/sdpsession.h
        include/promeki/socketaddress.h
        include/promeki/tcpserver.h
        include/promeki/tcpsocket.h
        include/promeki/udpsocket.h
    )
    list(APPEND PROMEKI_SOURCES
        src/network/abstractsocket.cpp
        src/network/ipv4address.cpp
        src/network/ipv6address.cpp
        src/network/macaddress.cpp
        src/network/multicastmanager.cpp
        src/network/networkaddress.cpp
        src/network/prioritysocket.cpp
        src/network/rawsocket.cpp
        src/network/rtppayload.cpp
        src/network/rtpsession.cpp
        src/network/sdpsession.cpp
        src/network/socketaddress.cpp
        src/network/tcpserver.cpp
        src/network/tcpsocket.cpp
        src/network/udpsocket.cpp
    )
endif()

# ProAV sources
if(PROMEKI_ENABLE_PROAV)
    list(APPEND PROMEKI_HEADERS
        include/promeki/audio.h
        include/promeki/audioblock.h
        include/promeki/audiodesc.h
        include/promeki/audiofile.h
        include/promeki/audiofilefactory.h
        include/promeki/audiogen.h
        include/promeki/audiotestpattern.h
        include/promeki/basicfont.h
        include/promeki/benchmarkreporter.h
        include/promeki/codec.h
        include/promeki/encodeddesc.h
        include/promeki/fastfont.h
        include/promeki/font.h
        include/promeki/frame.h
        include/promeki/image.h
        include/promeki/imagedesc.h
        include/promeki/imagefile.h
        include/promeki/imagefileio.h
        include/promeki/jpegimagecodec.h
        include/promeki/jpegencodernode.h
        include/promeki/ltcdecoder.h
        include/promeki/ltcencoder.h
        include/promeki/mediaio.h
        include/promeki/mediaiotask.h
        include/promeki/mediaiotask_imagefile.h
        include/promeki/mediaiotask_tpg.h
        include/promeki/medianode.h
        include/promeki/medianodeconfig.h
        include/promeki/mediapipeline.h
        include/promeki/mediapipelineconfig.h
        include/promeki/mediasink.h
        include/promeki/mediasource.h
        include/promeki/paintengine.h
        include/promeki/testpatternnode.h
        include/promeki/timecodeoverlaynode.h
        include/promeki/mediadesc.h
        include/promeki/videodesc.h
        include/promeki/videotestpattern.h
    )
    list(APPEND PROMEKI_SOURCES
        src/proav/audio.cpp
        src/proav/audioblock.cpp
        src/proav/audiodesc.cpp
        src/proav/audiofile.cpp
        src/proav/audiofilefactory.cpp
        src/proav/audiogen.cpp
        src/proav/audiotestpattern.cpp
        src/proav/benchmarkreporter.cpp
        src/proav/codec.cpp
        src/proav/encodeddesc.cpp
        src/proav/frame.cpp
        src/proav/framedemuxnode.cpp
        src/proav/image.cpp
        src/proav/imagedesc.cpp
        src/proav/imagefile.cpp
        src/proav/imagefileio.cpp
        src/proav/imagefileio_rawyuv.cpp
        src/proav/imagefileio_dpx.cpp
        src/proav/imagefileio_cin.cpp
        src/proav/imagefileio_tga.cpp
        src/proav/imagefileio_sgi.cpp
        src/proav/imagefileio_pnm.cpp
        src/proav/jpegimagecodec.cpp
        src/proav/jpegencodernode.cpp
        src/proav/ltcdecoder.cpp
        src/proav/ltcencoder.cpp
        src/proav/mediaio.cpp
        src/proav/mediaiotask.cpp
        src/proav/mediaiotask_imagefile.cpp
        src/proav/mediaiotask_tpg.cpp
        src/proav/medianode.cpp
        src/proav/medianodeconfig.cpp
        src/proav/mediapipeline.cpp
        src/proav/mediapipelineconfig.cpp
        src/proav/mediasink.cpp
        src/proav/mediasource.cpp
        src/proav/paintengine.cpp
        src/proav/paintengine_interleaved.cpp
        src/proav/testpatternnode.cpp
        src/proav/timecodeoverlaynode.cpp
        src/proav/mediadesc.cpp
        src/proav/videodesc.cpp
        src/proav/videotestpattern.cpp
    )

    # RTP sink nodes require networking
    if(PROMEKI_ENABLE_NETWORK)
        list(APPEND PROMEKI_HEADERS
            include/promeki/rtpaudiosinknode.h
            include/promeki/rtpvideosinknode.h
        )
        list(APPEND PROMEKI_SOURCES
            src/proav/rtpaudiosinknode.cpp
            src/proav/rtpvideosinknode.cpp
        )
    endif()

    # Feature-gated proav sources
    if(PROMEKI_ENABLE_JPEG)
        # jpegimagecodec is always included when proav is on; this gate
        # controls linking against libjpeg (codec uses it internally)
    endif()
    if(PROMEKI_ENABLE_PNG)
        list(APPEND PROMEKI_SOURCES src/proav/imagefileio_png.cpp)
    endif()
    if(PROMEKI_ENABLE_FREETYPE)
        list(APPEND PROMEKI_SOURCES
            src/proav/font.cpp
            src/proav/fastfont.cpp
            src/proav/basicfont.cpp
        )
    endif()
    if(PROMEKI_ENABLE_AUDIO)
        list(APPEND PROMEKI_HEADERS include/promeki/mediaiotask_audiofile.h)
        list(APPEND PROMEKI_SOURCES
            src/proav/audiofile_libsndfile.cpp
            src/proav/mediaiotask_audiofile.cpp
        )
    endif()
    if(PROMEKI_ENABLE_CSC)
        list(APPEND PROMEKI_HEADERS
            include/promeki/cscpipeline.h
            include/promeki/csccontext.h
            include/promeki/cscregistry.h
        )
        list(APPEND PROMEKI_SOURCES
            src/proav/csc/cscpipeline.cpp
            src/proav/csc/csccontext.cpp
            src/proav/csc/cscregistry.cpp
            src/proav/csc/unpack.cpp
            src/proav/csc/pack.cpp
            src/proav/csc/transfer.cpp
            src/proav/csc/matrix.cpp
            src/proav/csc/chroma.cpp
            src/proav/csc/alpha.cpp
            src/proav/csc/fastpath.cpp
        )
    endif()
endif()

# Music sources
if(PROMEKI_ENABLE_MUSIC)
    list(APPEND PROMEKI_HEADERS
        include/promeki/midinote.h
        include/promeki/midinotenames.h
        include/promeki/musicalnote.h
        include/promeki/musicalscale.h
        include/promeki/notesequenceparser.h
    )
    list(APPEND PROMEKI_SOURCES
        src/music/midinote.cpp
        src/music/midinotenames.cpp
        src/music/musicalscale.cpp
        src/music/notesequenceparser.cpp
    )
endif()

add_library(promeki SHARED ${PROMEKI_SOURCES})
add_library(promeki::promeki ALIAS promeki)
set_target_properties(promeki PROPERTIES
    VERSION ${APP_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME promeki
    EXPORT_NAME promeki
)

# Include directories
target_include_directories(promeki PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# nlohmann-json include paths (headers use #include <nlohmann/json.hpp>)
if(PROMEKI_USE_SYSTEM_NLOHMANN_JSON)
    target_link_libraries(promeki PUBLIC nlohmann_json::nlohmann_json)
else()
    target_include_directories(promeki PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann-json/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty>
    )
endif()

# libvtc include paths
if(PROMEKI_USE_SYSTEM_VTC)
    target_link_libraries(promeki PRIVATE ${PROMEKI_VTC_TARGET})
else()
    target_include_directories(promeki PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libvtc/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty>
    )
    target_link_libraries(promeki PRIVATE ${PROMEKI_VTC_TARGET})
endif()

target_link_libraries(promeki PUBLIC stdc++fs)
target_link_libraries(promeki PRIVATE m)
target_compile_definitions(promeki PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)

# Vendored thirdparty include paths and link dependencies
if(PROMEKI_ENABLE_PROAV)
    # When using vendored deps, we need their include paths for compilation
    if(NOT PROMEKI_USE_SYSTEM_LIBJPEG AND PROMEKI_ENABLE_JPEG)
        target_include_directories(promeki PRIVATE ${PROMEKI_THIRDPARTY_PREFIX}/include)
    endif()
    if(NOT PROMEKI_USE_SYSTEM_LIBSPNG AND PROMEKI_ENABLE_PNG)
        target_include_directories(promeki PRIVATE ${PROMEKI_THIRDPARTY_PREFIX}/include)
    endif()
    if(NOT PROMEKI_USE_SYSTEM_SNDFILE AND PROMEKI_ENABLE_AUDIO)
        target_include_directories(promeki PRIVATE ${PROMEKI_THIRDPARTY_PREFIX}/include)
    endif()
    if(NOT PROMEKI_USE_SYSTEM_FREETYPE AND PROMEKI_ENABLE_FREETYPE)
        target_include_directories(promeki PRIVATE
            ${PROMEKI_THIRDPARTY_PREFIX}/include
            ${PROMEKI_THIRDPARTY_PREFIX}/include/freetype2
        )
    endif()

    if(PROMEKI_ENABLE_JPEG)
        target_link_libraries(promeki PRIVATE ${PROMEKI_JPEG_TARGET})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_JPEG)
    endif()
    if(PROMEKI_ENABLE_PNG)
        target_link_libraries(promeki PRIVATE ${PROMEKI_SPNG_TARGET} ${PROMEKI_ZLIB_TARGET})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_PNG SPNG_STATIC)
    endif()
    if(PROMEKI_ENABLE_FREETYPE)
        target_link_libraries(promeki PRIVATE ${PROMEKI_FREETYPE_TARGET} ${PROMEKI_ZLIB_TARGET})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_FREETYPE)
    endif()
    if(PROMEKI_ENABLE_AUDIO)
        target_link_libraries(promeki PRIVATE ${PROMEKI_SNDFILE_TARGET})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_SNDFILE)
    endif()
    if(PROMEKI_ENABLE_CSC)
        target_link_libraries(promeki PRIVATE ${PROMEKI_HWY_TARGET})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_CSC)
        # Highway foreach_target needs to resolve includes relative to source root
        target_include_directories(promeki PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
endif()

# TUI Library ##################################################################

if(PROMEKI_BUILD_TUI)
    set(PROMEKI_TUI_HEADERS
        include/promeki/tui/application.h
        include/promeki/tui/button.h
        include/promeki/tui/checkbox.h
        include/promeki/tui/frame.h
        include/promeki/tui/inputparser.h
        include/promeki/tui/label.h
        include/promeki/tui/layout.h
        include/promeki/tui/lineedit.h
        include/promeki/tui/listview.h
        include/promeki/tui/menu.h
        include/promeki/tui/painter.h
        include/promeki/tui/palette.h
        include/promeki/tui/progressbar.h
        include/promeki/tui/screen.h
        include/promeki/tui/scrollarea.h
        include/promeki/tui/splitter.h
        include/promeki/tui/statusbar.h
        include/promeki/tui/style.h
        include/promeki/tui/tabwidget.h
        include/promeki/tui/textarea.h
        include/promeki/tui/widget.h
    )

    set(PROMEKI_TUI_SOURCES
        src/tui/application.cpp
        src/tui/button.cpp
        src/tui/checkbox.cpp
        src/tui/frame.cpp
        src/tui/inputparser.cpp
        src/tui/label.cpp
        src/tui/lineedit.cpp
        src/tui/listview.cpp
        src/tui/menu.cpp
        src/tui/painter.cpp
        src/tui/palette.cpp
        src/tui/progressbar.cpp
        src/tui/screen.cpp
        src/tui/scrollarea.cpp
        src/tui/splitter.cpp
        src/tui/statusbar.cpp
        src/tui/style.cpp
        src/tui/tabwidget.cpp
        src/tui/textarea.cpp
        src/tui/widget.cpp
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
    target_link_libraries(promeki-tui PUBLIC promeki)
    target_compile_definitions(promeki-tui PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)
endif()

# SDL Library ##################################################################

if(PROMEKI_BUILD_SDL)
    find_package(SDL3 REQUIRED)

    set(PROMEKI_SDL_HEADERS
        include/promeki/sdl/sdlapplication.h
        include/promeki/sdl/sdlaudiooutput.h
        include/promeki/sdl/sdldisplaynode.h
        include/promeki/sdl/sdleventpump.h
        include/promeki/sdl/sdlplayer.h
        include/promeki/sdl/sdlvideowidget.h
        include/promeki/sdl/sdlwindow.h
    )

    set(PROMEKI_SDL_SOURCES
        src/sdl/sdlapplication.cpp
        src/sdl/sdlaudiooutput.cpp
        src/sdl/sdldisplaynode.cpp
        src/sdl/sdleventpump.cpp
        src/sdl/sdlplayer.cpp
        src/sdl/sdlvideowidget.cpp
        src/sdl/sdlwindow.cpp
    )

    add_library(promeki-sdl SHARED ${PROMEKI_SDL_SOURCES})
    add_library(promeki::sdl ALIAS promeki-sdl)
    set_target_properties(promeki-sdl PROPERTIES
        VERSION ${APP_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        OUTPUT_NAME promeki-sdl
        EXPORT_NAME sdl
    )
    target_include_directories(promeki-sdl PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(promeki-sdl PUBLIC promeki SDL3::SDL3)
    target_compile_definitions(promeki-sdl PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)
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

# Main library
install(TARGETS promeki
    EXPORT promeki-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install headers (flat under promeki/, excluding tui and sdl subdirs)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/promeki/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki
    FILES_MATCHING PATTERN "*.h"
    PATTERN "tui" EXCLUDE
    PATTERN "sdl" EXCLUDE
    PATTERN "*.h.in" EXCLUDE
)

# Install generated config.h
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/promeki/config.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki
)

# Install vendored nlohmann headers (needed by consumers) unless using system
if(NOT PROMEKI_USE_SYSTEM_NLOHMANN_JSON)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann-json/include/nlohmann
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty
    )
endif()

# Install vendored libvtc headers (needed by consumers) unless using system
if(NOT PROMEKI_USE_SYSTEM_VTC)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libvtc/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty
    )
endif()

# Install doctest headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/doctest
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty
)

# Export main targets
install(EXPORT promeki-targets
    FILE promeki-targets.cmake
    NAMESPACE promeki::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/promeki
)

# TUI library
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

# SDL library
if(PROMEKI_BUILD_SDL)
    install(TARGETS promeki-sdl
        EXPORT promeki-sdl-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(FILES ${PROMEKI_SDL_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki/sdl
    )
    install(EXPORT promeki-sdl-targets
        FILE promeki-sdl-targets.cmake
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
        tests/algorithm.cpp
        tests/ansistream.cpp
        tests/application.cpp
        tests/array.cpp
        tests/atomic.cpp
        tests/audiolevel.cpp
        tests/buffer.cpp
        tests/bufferediodevice.cpp
        tests/bufferiodevice.cpp
        tests/bufferview.cpp
        tests/buildinfo.cpp
        tests/bytearray.cpp
        tests/ciepoint.cpp
        tests/ciewavelengthtable.cpp
        tests/cmdlineparser.cpp
        tests/color.cpp
        tests/colormodel.cpp
        tests/configoption.cpp
        tests/datastream.cpp
        tests/datetime.cpp
        tests/deque.cpp
        tests/dir.cpp
        tests/duration.cpp
        tests/elapsedtimer.cpp
        tests/env.cpp
        tests/error.cpp
        tests/event.cpp
        tests/eventloop.cpp
        tests/file.cpp
        tests/fileformatfactory.cpp
        tests/fileinfo.cpp
        tests/fileiodevice.cpp
        tests/filepath.cpp
        tests/fnv1a.cpp
        tests/fourcc.cpp
        tests/framerate.cpp
        tests/future.cpp
        tests/hashmap.cpp
        tests/hashset.cpp
        tests/iodevice.cpp
        tests/json.cpp
        tests/line.cpp
        tests/list.cpp
        tests/logger.cpp
        tests/map.cpp
        tests/matrix.cpp
        tests/matrix3x3.cpp
        tests/md5.cpp
        tests/mempool.cpp
        tests/memspace.cpp
        tests/metadata.cpp
        tests/mutex.cpp
        tests/numname.cpp
        tests/obfuscatedstring.cpp
        tests/objectbase.cpp
        tests/objectbase2.cpp
        tests/pair.cpp
        tests/pidcontroller.cpp
        tests/pixelformat.cpp
        tests/point.cpp
        tests/priorityqueue.cpp
        tests/process.cpp
        tests/queue.cpp
        tests/random.cpp
        tests/rational.cpp
        tests/readwritelock.cpp
        tests/rect.cpp
        tests/regex.cpp
        tests/result.cpp
        tests/securebuffer.cpp
        tests/set.cpp
        tests/sha1.cpp
        tests/sharedptr.cpp
        tests/signal.cpp
        tests/size2d.cpp
        tests/span.cpp
        tests/stack.cpp
        tests/streamstring.cpp
        tests/string.cpp
        tests/stringiodevice.cpp
        tests/stringlist.cpp
        tests/stringregistry.cpp
        tests/strand.cpp
        tests/structdatabase.cpp
        tests/system.cpp
        tests/terminal.cpp
        tests/textstream.cpp
        tests/thread.cpp
        tests/threadpool.cpp
        tests/timecode.cpp
        tests/timecodegenerator.cpp
        tests/timestamp.cpp
        tests/uuid.cpp
        tests/variant.cpp
        tests/variantdatabase.cpp
        tests/waitcondition.cpp
        tests/xyzcolor.cpp
    )

    if(PROMEKI_ENABLE_NETWORK)
        list(APPEND UNITTEST_SOURCES
            tests/network/abstractsocket.cpp
            tests/network/ipv4address.cpp
            tests/network/ipv6address.cpp
            tests/network/macaddress.cpp
            tests/network/multicastmanager.cpp
            tests/network/networkaddress.cpp
            tests/network/prioritysocket.cpp
            tests/network/rawsocket.cpp
            tests/network/rtppacket.cpp
            tests/network/rtppayload.cpp
            tests/network/rtpsession.cpp
            tests/network/sdpsession.cpp
            tests/network/socketaddress.cpp
            tests/network/tcpserver.cpp
            tests/network/tcpsocket.cpp
            tests/network/udpsocket.cpp
        )
    endif()

    if(PROMEKI_ENABLE_PROAV)
        list(APPEND UNITTEST_SOURCES
            tests/audio.cpp
            tests/audioblock.cpp
            tests/audiodesc.cpp
            tests/audiofilefactory.cpp
            tests/audiotestpattern.cpp
            tests/codec.cpp
            tests/encodeddesc.cpp
            tests/fastfont.cpp
            tests/basicfont.cpp
            tests/frame.cpp
            tests/framedemuxnode.cpp
            tests/image.cpp
            tests/imagedesc.cpp
            tests/imagefile.cpp
            tests/imagefileio.cpp
            tests/imagefileio_dpx.cpp
            tests/imagefileio_cin.cpp
            tests/imagefileio_tga.cpp
            tests/imagefileio_sgi.cpp
            tests/imagefileio_pnm.cpp
            tests/jpegimagecodec.cpp
            tests/jpegencodernode.cpp
            tests/ltcdecoder.cpp
            tests/ltcencoder.cpp
            tests/mediaio.cpp
            tests/medianode.cpp
            tests/medianodeconfig.cpp
            tests/mediapipeline.cpp
            tests/mediapipelineconfig.cpp
            tests/mediasink.cpp
            tests/mediasource.cpp
            tests/paintengine.cpp
            tests/pixeldesc.cpp
            tests/testpatternnode.cpp
            tests/timecodeoverlaynode.cpp
            tests/videodesc.cpp
            tests/videotestpattern.cpp
        )
        if(PROMEKI_ENABLE_NETWORK)
            list(APPEND UNITTEST_SOURCES
                tests/rtpaudiosinknode.cpp
                tests/rtpvideosinknode.cpp
            )
        endif()
        if(PROMEKI_ENABLE_CSC)
            list(APPEND UNITTEST_SOURCES
                tests/cscpipeline.cpp
            )
        endif()
        if(PROMEKI_ENABLE_PNG)
            list(APPEND UNITTEST_SOURCES
                tests/imagefileio_png.cpp
            )
        endif()
    endif()

    if(PROMEKI_ENABLE_MUSIC)
        list(APPEND UNITTEST_SOURCES
            tests/midinote.cpp
            tests/midinotenames.cpp
            tests/musicalnote.cpp
            tests/musicalscale.cpp
            tests/notesequenceparser.cpp
        )
    endif()

    add_executable(unittest-promeki ${UNITTEST_SOURCES})
    target_compile_definitions(unittest-promeki PRIVATE
        $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>
        PROMEKI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_include_directories(unittest-promeki PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
    target_link_libraries(unittest-promeki PRIVATE promeki)
    add_test(NAME unittest-promeki COMMAND unittest-promeki)
    set_tests_properties(unittest-promeki PROPERTIES TIMEOUT 60)

    if(PROMEKI_BUILD_TUI)
        set(UNITTEST_TUI_SOURCES
            tests/doctest_main.cpp
            tests/tui/button.cpp
            tests/tui/checkbox.cpp
            tests/tui/frame.cpp
            tests/tui/inputparser.cpp
            tests/tui/label.cpp
            tests/tui/layout.cpp
            tests/tui/lineedit.cpp
            tests/tui/listview.cpp
            tests/tui/menu.cpp
            tests/tui/painter.cpp
            tests/tui/palette.cpp
            tests/tui/progressbar.cpp
            tests/tui/screen.cpp
            tests/tui/scrollarea.cpp
            tests/tui/splitter.cpp
            tests/tui/statusbar.cpp
            tests/tui/tabwidget.cpp
            tests/tui/textarea.cpp
            tests/tui/widget.cpp
        )
        add_executable(unittest-tui ${UNITTEST_TUI_SOURCES})
        target_compile_definitions(unittest-tui PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)
        target_include_directories(unittest-tui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
        target_link_libraries(unittest-tui PRIVATE promeki-tui)
        add_test(NAME unittest-tui COMMAND unittest-tui)
        set_tests_properties(unittest-tui PROPERTIES TIMEOUT 60)
    endif()

    if(PROMEKI_BUILD_SDL)
        set(UNITTEST_SDL_SOURCES
            tests/doctest_main.cpp
            tests/sdl/sdlrenderer.cpp
        )
        add_executable(unittest-sdl ${UNITTEST_SDL_SOURCES})
        target_compile_definitions(unittest-sdl PRIVATE $<$<CONFIG:Debug>:PROMEKI_DEBUG_ENABLE>)
        target_include_directories(unittest-sdl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
        target_link_libraries(unittest-sdl PRIVATE promeki-sdl)
        add_test(NAME unittest-sdl COMMAND unittest-sdl)
        set_tests_properties(unittest-sdl PROPERTIES TIMEOUT 60)
    endif()

    # Run tests as part of the normal build
    set(PROMEKI_TEST_TARGETS unittest-promeki)
    if(PROMEKI_BUILD_TUI)
        list(APPEND PROMEKI_TEST_TARGETS unittest-tui)
    endif()
    if(PROMEKI_BUILD_SDL)
        list(APPEND PROMEKI_TEST_TARGETS unittest-sdl)
    endif()

    add_custom_target(run-tests ALL
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS ${PROMEKI_TEST_TARGETS}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running unit tests"
    )
endif()

# Custom Clean Targets #########################################################

set(PROMEKI_CLEAN_DIRS
    ${CMAKE_BINARY_DIR}/CMakeFiles/promeki.dir
    ${CMAKE_BINARY_DIR}/CMakeFiles/libpromeki_invalidate_build_info.dir
    ${CMAKE_BINARY_DIR}/buildinfo.cpp
    ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libpromeki.*
    ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libpromeki-*.so*
)

if(PROMEKI_BUILD_TESTS)
    list(APPEND PROMEKI_CLEAN_DIRS
        ${CMAKE_BINARY_DIR}/CMakeFiles/unittest-promeki.dir
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unittest-promeki
        ${CMAKE_BINARY_DIR}/CMakeFiles/run-tests.dir
    )
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

if(PROMEKI_BUILD_SDL)
    list(APPEND PROMEKI_CLEAN_DIRS
        ${CMAKE_BINARY_DIR}/CMakeFiles/promeki-sdl.dir
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libpromeki-sdl*
    )
    if(PROMEKI_BUILD_TESTS)
        list(APPEND PROMEKI_CLEAN_DIRS
            ${CMAKE_BINARY_DIR}/CMakeFiles/unittest-sdl.dir
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unittest-sdl
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
