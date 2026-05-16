# Full promeki build — every feature explicitly enabled.
#
# Forces ON every PROMEKI_ENABLE_* / PROMEKI_BUILD_* flag, including the
# normally probe-gated ones (CUDA, NVENC, NVDEC, NDI, JPEG XS, V4L2,
# memfd).  CMake configure will hard-error if any of these are missing
# their host capability or SDK — useful for CI matrices that want to
# guarantee a full-featured artifact and notice immediately when the
# build environment regresses.
#
# Set the SDK path overrides at the bottom of this file before configuring
# on a machine where the SDKs live in a non-standard location.

# ---------------------------------------------------------------------------
# Component builds — everything.
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_BUILD_TUI         ON)
promeki_config_option(PROMEKI_BUILD_SDL         ON)
promeki_config_option(PROMEKI_BUILD_UTILS       ON)
promeki_config_option(PROMEKI_BUILD_DEMOS       ON)
promeki_config_option(PROMEKI_BUILD_TESTS       ON)
promeki_config_option(PROMEKI_BUILD_DOCS        ON)
promeki_config_option(PROMEKI_BUILD_BENCHMARKS  ON)
promeki_config_option(PROMEKI_BUILD_STATS       ON)

# ---------------------------------------------------------------------------
# Build hygiene — strict.
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_WARNINGS_AS_ERRORS ON)
promeki_config_option(PROMEKI_USE_PCH            ON)
promeki_config_option(PROMEKI_USE_CCACHE         ON)

# ---------------------------------------------------------------------------
# Feature flags — all ON, including probe-gated ones.
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_ENABLE_NETWORK   ON)
promeki_config_option(PROMEKI_ENABLE_HTTP      ON)
promeki_config_option(PROMEKI_ENABLE_TLS       ON)
promeki_config_option(PROMEKI_ENABLE_SRT       ON)
promeki_config_option(PROMEKI_ENABLE_PROAV     ON)
promeki_config_option(PROMEKI_ENABLE_MUSIC     ON)
promeki_config_option(PROMEKI_ENABLE_PNG       ON)
promeki_config_option(PROMEKI_ENABLE_JPEG      ON)
promeki_config_option(PROMEKI_ENABLE_JPEGXS    ON)
promeki_config_option(PROMEKI_ENABLE_FREETYPE  ON)
promeki_config_option(PROMEKI_ENABLE_AUDIO     ON)
promeki_config_option(PROMEKI_ENABLE_OPUS      ON)
promeki_config_option(PROMEKI_ENABLE_AAC       ON)
promeki_config_option(PROMEKI_ENABLE_SRC       ON)
promeki_config_option(PROMEKI_ENABLE_CSC       ON)
promeki_config_option(PROMEKI_ENABLE_CIRF      ON)
promeki_config_option(PROMEKI_ENABLE_V4L2      ON)
promeki_config_option(PROMEKI_ENABLE_MEMFD     ON)
promeki_config_option(PROMEKI_ENABLE_CUDA      ON)
promeki_config_option(PROMEKI_ENABLE_NVENC     ON)
promeki_config_option(PROMEKI_ENABLE_NVDEC     ON)
promeki_config_option(PROMEKI_ENABLE_NDI       ON)

# ---------------------------------------------------------------------------
# External SDK paths — uncomment + edit on hosts where the SDKs live
# outside the standard locations or are not on PATH for find_package /
# find_path probes.
# ---------------------------------------------------------------------------
# promeki_config_path(PROMEKI_NVENC_SDK_DIR "/opt/nvidia/video-codec-sdk")
# promeki_config_path(PROMEKI_NDI_SDK_DIR   "/opt/ndi/sdk")
