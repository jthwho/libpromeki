# Debug build preset.
#
# Forces CMAKE_BUILD_TYPE=Debug (full DWARF debug info, -O0, asserts on)
# and keeps the rest of the project at its normal defaults — no opinion
# on features.  Combine with another preset via include() if you want
# Debug build flags applied to e.g. a minimal feature set:
#
#   include(${CMAKE_CURRENT_LIST_DIR}/minimal.cmake)
#   include(${CMAKE_CURRENT_LIST_DIR}/debug.cmake)
#
# (Order matters: the later include wins on overlapping settings.)

promeki_config_string(CMAKE_BUILD_TYPE "Debug")

# PCH and ccache stay ON by default; both work fine with -O0 -g.  ccache
# distinguishes Debug from DevRelease via flag hash, so the cache won't
# collide.
