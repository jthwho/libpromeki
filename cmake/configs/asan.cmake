# AddressSanitizer + UndefinedBehaviorSanitizer preset.
#
# Debug build with -fsanitize=address,undefined applied to every target
# (library, executables, tests, vendored deps).  PCH and ccache are
# automatically disabled by the top-level CMakeLists when a sanitizer
# is enabled, so you don't need to turn them off here.
#
# Useful for unit-test runs and reproducing memory issues.  Expect
# 2-3x runtime overhead and roughly 2x peak RSS compared to a clean
# DevRelease build.  For thread-issue hunting, copy this file and
# swap address,undefined for thread (ASan and TSan are mutually
# exclusive).

promeki_config_string(CMAKE_BUILD_TYPE "Debug")
promeki_config_string(PROMEKI_SANITIZER "address,undefined")
