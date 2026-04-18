# Promeki Functional Tests
#
# This directory contains small applications that exercise the promeki
# library against real hardware or other conditions that are out of
# scope for unit tests.  They are not demos (which teach API usage)
# and not utilities (which ship as end-user tools).  They exist so
# developers can quickly validate a specific subsystem end-to-end.
#
# Functional tests are NOT built as part of the normal project build.
# From the top-level project they are included with EXCLUDE_FROM_ALL
# so that `cmake --build <builddir>` will not touch them, but an
# individual test can be built on demand by naming its target, e.g.:
#
#     cmake --build <builddir> --target sdlaudioclock-functest
#
# Or, equivalently through the repo's `build` helper:
#
#     build sdlaudioclock-functest
#
# This directory may also be configured as a standalone project, in
# which case it uses find_package(promeki) to locate an installed
# copy of the library — mirroring the pattern used by utils/ and
# demos/.

cmake_minimum_required(VERSION 3.22)
project(promeki-functests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# When built standalone, find the installed promeki library.
# When built as part of the main project, the targets already exist.
if(NOT TARGET promeki::promeki)
    find_package(promeki REQUIRED)
endif()

# Whether SDL-based functional tests can be built.  In-tree this
# tracks the parent project's PROMEKI_BUILD_SDL; standalone we probe
# for SDL3 directly.
if(TARGET promeki::sdl)
    set(PROMEKI_FUNCTEST_HAVE_SDL TRUE)
else()
    find_package(SDL3 QUIET)
    if(SDL3_FOUND)
        set(PROMEKI_FUNCTEST_HAVE_SDL TRUE)
    else()
        set(PROMEKI_FUNCTEST_HAVE_SDL FALSE)
    endif()
endif()

# framesync is a headless FrameSync/WallClock smoke test and has no
# SDL dependency — it only needs promeki::promeki, plus whatever
# capture MediaIO backend the user picks at runtime (V4L2 by default
# on Linux builds that have it enabled).
add_subdirectory(framesync)
add_subdirectory(paintengine)

if(PROMEKI_ENABLE_NVENC)
    add_subdirectory(nvenc)
endif()

if(PROMEKI_FUNCTEST_HAVE_SDL)
    add_subdirectory(sdlaudioclock)
endif()
