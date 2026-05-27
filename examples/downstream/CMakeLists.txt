# Example downstream consumer of libpromeki.
#
# This is a self-contained, out-of-tree CMake project that links a small
# application against libpromeki.  It demonstrates the two supported ways
# to consume the library and — importantly — the correct way to drive
# libpromeki's feature flags from the consuming build.
#
# It is NOT part of the libpromeki build itself; copy this directory
# somewhere, or point CMake at it directly:
#
#     cmake -S examples/downstream -B /tmp/promeki-example
#     cmake --build /tmp/promeki-example
#
# See docs/building.md (section "Using libpromeki in Your Project") for
# the full write-up.

cmake_minimum_required(VERSION 3.22)
project(promeki_downstream_example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Choose how to bring libpromeki in.
#
#   EXAMPLE_USE_SUBDIR=OFF (default) — find_package(promeki): consume an
#       already-built-and-installed libpromeki.  This is the recommended
#       path for packaged / cross / Yocto builds, where libpromeki is its
#       own build artifact (or recipe) and the application just links the
#       imported target.  The library's feature set was fixed when it was
#       built; the application does not — and cannot — change it here.
#
#   EXAMPLE_USE_SUBDIR=ON — add_subdirectory(): build libpromeki from
#       source as part of this build.  This is the path that lets the
#       application choose libpromeki's feature flags, and the block below
#       shows how to do that *correctly*.
# ---------------------------------------------------------------------------
option(EXAMPLE_USE_SUBDIR "Build libpromeki from source via add_subdirectory instead of find_package" OFF)

if(EXAMPLE_USE_SUBDIR)
    # Where the libpromeki source tree lives relative to this file.  Adjust
    # to wherever you vendor it (a submodule, a sibling checkout, ...).
    set(PROMEKI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH
        "Path to the libpromeki source tree")

    # === Setting libpromeki's feature flags from this build ================
    #
    # RULE 1: set the flags BEFORE add_subdirectory().  libpromeki reads
    #         them while it configures; setting them afterwards has no
    #         effect.
    #
    # RULE 2: a plain set() (no CACHE, no FORCE) is enough.  libpromeki
    #         declares each flag with option()/set(CACHE), and CMake policy
    #         CMP0077 (NEW since 3.13; libpromeki requires 3.22) makes a
    #         normal variable of the same name win over the cached default.
    #         You do NOT need `CACHE BOOL "" FORCE` — that is heavier than
    #         necessary and the empty doc-string is noise.
    #
    # RULE 3: prefer a stock preset over a hand-maintained wall of flags.
    #         libpromeki ships presets under cmake/configs/ that are kept in
    #         sync with the flag set and exercised by the project's own
    #         builds.  Point PROMEKI_CONFIG_FILE at one and you inherit a
    #         coherent, tested configuration instead of a list that drifts
    #         every time a flag is added or removed.  An ANC / SDI appliance,
    #         for example, wants the "proav-embedded" preset:
    set(PROMEKI_CONFIG_FILE proav-embedded)

    # If you genuinely need to deviate from the preset, override individual
    # flags here (still before add_subdirectory).  These win over the
    # preset's defaults for plain option() flags.  Example: this box has an
    # AJA card, so turn its backend on explicitly.
    #
    #     set(PROMEKI_ENABLE_NTV2 ON)
    #
    # Or, if you are not using a preset at all, set flags one by one:
    #
    #     set(PROMEKI_ENABLE_NETWORK OFF)
    #     set(PROMEKI_ENABLE_PROAV   ON)
    #     set(PROMEKI_ENABLE_JPEG    OFF)
    #     ...

    # RULE 4: do NOT try to shrink the build with
    #         target_compile_options(promeki PRIVATE -g0 -Os) after the
    #         fact.  Optimisation level and debug info are controlled by the
    #         build type, which is global to the whole tree.  Set it once,
    #         here, before add_subdirectory().  (In a packaged build, let
    #         the packaging system — e.g. Yocto's -dbg split — handle debug
    #         info instead.)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release)
    endif()

    add_subdirectory("${PROMEKI_SOURCE_DIR}" promeki-build)
else()
    # find_package path.  If libpromeki was installed to a non-standard
    # prefix, pass -DCMAKE_PREFIX_PATH=/opt/promeki on the cmake line.
    find_package(promeki REQUIRED)
endif()

add_executable(promeki_example main.cpp)

# Either consumption mode exposes the same imported/alias target, so the
# link line is identical regardless of how the library was brought in.
target_link_libraries(promeki_example PRIVATE promeki::promeki)
