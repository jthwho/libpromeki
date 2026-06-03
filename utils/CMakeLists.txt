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

# Inherit the project's warning policy when built in-tree.  Standalone
# builds (find_package) skip this — the warnings interface lib is not
# part of the installed/exported promeki target set.
if(TARGET promeki::warnings)
    link_libraries(promeki::warnings)
endif()

add_subdirectory(promeki-info)

if(PROMEKI_BUILD_BENCHMARKS)
    add_subdirectory(promeki-bench)
endif()

if(PROMEKI_ENABLE_PROAV)
    add_subdirectory(imgtest)
    add_subdirectory(pmdf-inspect)
    add_subdirectory(promeki-test)
    # mediaplay builds with or without SDL: when promeki-sdl is
    # available it provides the default SDL display sink, otherwise
    # the user must supply an explicit -d destination.
    add_subdirectory(mediaplay)
endif()

# promeki-fetch-model — downloads ML model files (currently
# whisper.cpp GGML weights) into Dir::models() with optional
# SHA-256 verification.  Requires PROMEKI_ENABLE_HTTP for the
# HTTP / HTTPS client used to fetch from Hugging Face.
if(PROMEKI_ENABLE_HTTP AND PROMEKI_ENABLE_WHISPER)
    add_subdirectory(promeki-fetch-model)
endif()

# promeki-2110-calc — SMPTE ST 2110-21 traffic-shaping calculator CLI.
# Needs the network stack (St2110Tx / St2110Video) and proav
# (VideoFormat / PixelFormat).
if(PROMEKI_ENABLE_NETWORK AND PROMEKI_ENABLE_PROAV)
    add_subdirectory(promeki-2110-calc)
endif()

# promeki-pcap — offline pcap / pcapng decoder for SMPTE ST 2110-40 ANC.
# Needs the network stack (PcapReader / PacketDemux / PcapFlowRouter)
# and proav (AncPacket / St291Packet / AncFormat).
if(PROMEKI_ENABLE_NETWORK AND PROMEKI_ENABLE_PROAV)
    add_subdirectory(promeki-pcap)
endif()
