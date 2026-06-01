# Minimal promeki build — core library only.
#
# Smallest practical configuration: the core promeki library and its
# unit tests.  Networking, pro A/V, music, image / audio codecs,
# fonts, terminal UI, and SDL are all disabled.  Suitable for embedding
# promeki's data-object / variant / json / event-loop primitives into
# a host application that does its own I/O.

# ---------------------------------------------------------------------------
# Component builds
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_BUILD_TUI         OFF)
promeki_config_option(PROMEKI_BUILD_SDL         OFF)
promeki_config_option(PROMEKI_BUILD_UTILS       OFF)
promeki_config_option(PROMEKI_BUILD_DEMOS       OFF)
promeki_config_option(PROMEKI_BUILD_TESTS       ON)
promeki_config_option(PROMEKI_BUILD_DOCS        OFF)
promeki_config_option(PROMEKI_BUILD_BENCHMARKS  OFF)
promeki_config_option(PROMEKI_BUILD_STATS       OFF)

# ---------------------------------------------------------------------------
# Build hygiene — keep PCH + ccache for fast iteration.
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_WARNINGS_AS_ERRORS OFF)
promeki_config_option(PROMEKI_USE_PCH            ON)
promeki_config_option(PROMEKI_USE_CCACHE         ON)

# ---------------------------------------------------------------------------
# Feature flags — turn off every optional subsystem.
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_ENABLE_NETWORK   OFF)
promeki_config_option(PROMEKI_ENABLE_HTTP      OFF)
promeki_config_option(PROMEKI_ENABLE_TLS       OFF)
promeki_config_option(PROMEKI_ENABLE_SRT       OFF)
promeki_config_option(PROMEKI_ENABLE_PROAV     OFF)
promeki_config_option(PROMEKI_ENABLE_MUSIC     OFF)
promeki_config_option(PROMEKI_ENABLE_PNG       OFF)
promeki_config_option(PROMEKI_ENABLE_JPEG      OFF)
promeki_config_option(PROMEKI_ENABLE_JPEGXS    OFF)
# x264 H.264 software encoder — off (also requires PROAV, which is off).
# Keeps this minimal build non-GPL.
promeki_config_option(PROMEKI_ENABLE_X264      OFF)
promeki_config_option(PROMEKI_ENABLE_FREETYPE  OFF)
promeki_config_option(PROMEKI_ENABLE_AUDIO     OFF)
promeki_config_option(PROMEKI_ENABLE_OPUS      OFF)
promeki_config_option(PROMEKI_ENABLE_AAC       OFF)
promeki_config_option(PROMEKI_ENABLE_SRC       OFF)
promeki_config_option(PROMEKI_ENABLE_CSC       OFF)
promeki_config_option(PROMEKI_ENABLE_CIRF      OFF)
promeki_config_option(PROMEKI_ENABLE_V4L2      OFF)
promeki_config_option(PROMEKI_ENABLE_MEMFD     OFF)
promeki_config_option(PROMEKI_ENABLE_CUDA      OFF)
promeki_config_option(PROMEKI_ENABLE_NVENC     OFF)
promeki_config_option(PROMEKI_ENABLE_NVDEC     OFF)
promeki_config_option(PROMEKI_ENABLE_NDI       OFF)
promeki_config_option(PROMEKI_ENABLE_NTV2      OFF)
