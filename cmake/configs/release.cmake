# Release build preset.
#
# Forces CMAKE_BUILD_TYPE=Release (-O3 -DNDEBUG, no debug symbols by
# default).  Use this for distribution / production builds where the
# extra few-percent runtime cost of DevRelease's `-g1` is unwanted and
# you don't need crash-time symbol resolution from inside the binary.
#
# Compared to DevRelease (the project default):
#   - No debug info (-g1 stripped) — slimmer binaries, slightly faster
#     compiles, but core dumps and crash logs lose file:line resolution.
#   - promekiDebug() logging compiles to a no-op via NDEBUG.

promeki_config_string(CMAKE_BUILD_TYPE "Release")
