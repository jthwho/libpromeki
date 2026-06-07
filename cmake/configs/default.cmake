# Default promeki build preset.
#
# Mirrors the out-of-the-box defaults of CMakeLists.txt — included here
# so you can copy this file as a starting point for a custom config and
# see what each knob does without grepping the build system.
#
# Anything left commented out keeps the project default (which may be
# probe-driven: e.g. PROMEKI_ENABLE_CUDA defaults ON only when the CUDA
# toolkit is found).  Uncomment to pin the value regardless of probe
# results.

# ---------------------------------------------------------------------------
# Component builds
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_BUILD_TUI         ON)
# PROMEKI_BUILD_SDL defaults to ON when SDL3 is found, OFF otherwise.
# promeki_config_option(PROMEKI_BUILD_SDL       ON)
promeki_config_option(PROMEKI_BUILD_UTILS       ON)
promeki_config_option(PROMEKI_BUILD_DEMOS       ON)
promeki_config_option(PROMEKI_BUILD_TESTS       ON)
promeki_config_option(PROMEKI_BUILD_DOCS        OFF)
promeki_config_option(PROMEKI_BUILD_BENCHMARKS  ON)
promeki_config_option(PROMEKI_BUILD_STATS       ON)

# ---------------------------------------------------------------------------
# Build hygiene
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_WARNINGS_AS_ERRORS OFF)
promeki_config_option(PROMEKI_USE_PCH            ON)
promeki_config_option(PROMEKI_USE_CCACHE         ON)
promeki_config_string(PROMEKI_SANITIZER          "")

# ---------------------------------------------------------------------------
# Feature flags — all default ON unless probe-gated.
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_ENABLE_NETWORK   ON)
promeki_config_option(PROMEKI_ENABLE_HTTP      ON)
promeki_config_option(PROMEKI_ENABLE_TLS       ON)
promeki_config_option(PROMEKI_ENABLE_SRT       ON)
promeki_config_option(PROMEKI_ENABLE_PROAV     ON)
promeki_config_option(PROMEKI_ENABLE_MUSIC     ON)
promeki_config_option(PROMEKI_ENABLE_PNG       ON)
promeki_config_option(PROMEKI_ENABLE_JPEG      ON)
# x264 H.264 software encoder.  Default ON per project direction, but
# GPL-2.0-or-later: enabling it makes the whole libpromeki build subject
# to the GPL (configure prints a GPL COMPLIANCE NOTICE; see
# THIRD-PARTY-LICENSES).  Set OFF to keep a non-GPL build.
promeki_config_option(PROMEKI_ENABLE_X264      ON)
# PROMEKI_ENABLE_JPEGXS / V4L2 / MEMFD / CUDA / NVENC / NVDEC / NDI default
# ON only when the corresponding host capability or SDK is detected.  Leave
# them unset here to honour the probe, or pin them explicitly below.
# promeki_config_option(PROMEKI_ENABLE_JPEGXS  ON)
promeki_config_option(PROMEKI_ENABLE_FREETYPE  ON)
promeki_config_option(PROMEKI_ENABLE_AUDIO     ON)
promeki_config_option(PROMEKI_ENABLE_FLAC      ON)
promeki_config_option(PROMEKI_ENABLE_VORBIS    ON)
promeki_config_option(PROMEKI_ENABLE_MP3       ON)
promeki_config_option(PROMEKI_ENABLE_OPUS      ON)
promeki_config_option(PROMEKI_ENABLE_AAC       ON)
promeki_config_option(PROMEKI_ENABLE_FFMPEG    ON)
promeki_config_option(PROMEKI_ENABLE_SRC       ON)
promeki_config_option(PROMEKI_ENABLE_CSC       ON)
promeki_config_option(PROMEKI_ENABLE_CIRF      ON)
promeki_config_option(PROMEKI_ENABLE_NTV2      ON)
# promeki_config_option(PROMEKI_ENABLE_V4L2    ON)
# promeki_config_option(PROMEKI_ENABLE_MEMFD   ON)
# promeki_config_option(PROMEKI_ENABLE_CUDA    ON)
# promeki_config_option(PROMEKI_ENABLE_NVENC   ON)
# promeki_config_option(PROMEKI_ENABLE_NVDEC   ON)
# promeki_config_option(PROMEKI_ENABLE_NDI     ON)

# ---------------------------------------------------------------------------
# Vendored vs system deps — default to vendored for reproducible builds.
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_USE_SYSTEM_ZLIB          OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_LIBSPNG       OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_LIBJPEG       OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_SVT_JPEG_XS   OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_X264          OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_FREETYPE      OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_SNDFILE       OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_OPUS          OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_FDKAAC        OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_OGG           OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_FLAC          OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_VORBIS        OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_MPG123        OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_LAME          OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_SAMPLERATE    OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_NLOHMANN_JSON OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_PUGIXML       OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_VTC           OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_HIGHWAY       OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_CIRF          OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_LLHTTP        OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_MBEDTLS       OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_SRT           OFF)
promeki_config_option(PROMEKI_USE_SYSTEM_NTV2          OFF)
