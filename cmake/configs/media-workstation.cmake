# Media workstation preset — desktop development.
#
# Everything a developer working on the project from their workstation
# typically wants: all CPU codecs, fonts, terminal UI, SDL3 viewer,
# benchmarks, demos, utilities, tests.  GPU and SDK-gated backends
# (CUDA / NVENC / NVDEC / NDI / JPEG XS / V4L2 / memfd) stay
# probe-driven so the same config works on machines that have the
# hardware/SDK and machines that don't.

# ---------------------------------------------------------------------------
# Component builds
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_BUILD_TUI         ON)
promeki_config_option(PROMEKI_BUILD_SDL         ON)
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

# ---------------------------------------------------------------------------
# Feature flags
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_ENABLE_NETWORK   ON)
promeki_config_option(PROMEKI_ENABLE_HTTP      ON)
promeki_config_option(PROMEKI_ENABLE_TLS       ON)
promeki_config_option(PROMEKI_ENABLE_SRT       ON)
promeki_config_option(PROMEKI_ENABLE_PROAV     ON)
promeki_config_option(PROMEKI_ENABLE_MUSIC     ON)
promeki_config_option(PROMEKI_ENABLE_PNG       ON)
promeki_config_option(PROMEKI_ENABLE_JPEG      ON)
# x264 software H.264 encoder — a CPU codec, ON for desktop dev.
# GPL-2.0-or-later: enabling it makes this build GPL (set OFF to avoid).
promeki_config_option(PROMEKI_ENABLE_X264      ON)
promeki_config_option(PROMEKI_ENABLE_FREETYPE  ON)
promeki_config_option(PROMEKI_ENABLE_AUDIO     ON)
promeki_config_option(PROMEKI_ENABLE_OPUS      ON)
promeki_config_option(PROMEKI_ENABLE_AAC       ON)
promeki_config_option(PROMEKI_ENABLE_FFMPEG    ON)
promeki_config_option(PROMEKI_ENABLE_SRC       ON)
promeki_config_option(PROMEKI_ENABLE_CSC       ON)
promeki_config_option(PROMEKI_ENABLE_CIRF      ON)
# NTV2 is fully vendored — pin ON for desktop dev with AJA hardware.
promeki_config_option(PROMEKI_ENABLE_NTV2      ON)
# Probe-driven (deliberately not pinned):
#   PROMEKI_ENABLE_JPEGXS  — x86-64 + nasm/yasm
#   PROMEKI_ENABLE_V4L2    — Linux + ALSA + linux/videodev2.h
#   PROMEKI_ENABLE_MEMFD   — Linux + memfd_create probe
#   PROMEKI_ENABLE_CUDA    — find_package(CUDAToolkit)
#   PROMEKI_ENABLE_NVENC   — CUDA + Video Codec SDK headers
#   PROMEKI_ENABLE_NVDEC   — CUDA + Video Codec SDK headers
#   PROMEKI_ENABLE_NDI     — NDI SDK headers via PROMEKI_NDI_SDK_DIR
