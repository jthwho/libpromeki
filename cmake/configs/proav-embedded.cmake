# Embedded pro A/V preset — pro A/V core, no software codecs / fonts / audio.
#
# Target use case: lean embedded media devices (e.g. SDI ANC processors,
# capture / playout appliances) that need promeki's pro A/V frame, media-IO,
# and ANC machinery but deliberately leave out the heavyweight optional
# subsystems — software image / audio codecs (JPEG / JPEG XS), TrueType font
# rendering (FreeType), audio file I/O (libsndfile) and sample-rate
# conversion (libsamplerate).
#
# This is the configuration that surfaces "PROAV on, codecs off" link
# coverage: every pro A/V source file must build and link against the
# shared library with these features compiled out (no undefined references
# to JpegVideoCodec, FastFont, AudioResampler, etc.).

# ---------------------------------------------------------------------------
# Component builds
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_BUILD_TUI         OFF)
promeki_config_option(PROMEKI_BUILD_SDL         OFF)
promeki_config_option(PROMEKI_BUILD_UTILS       ON)
promeki_config_option(PROMEKI_BUILD_DEMOS       OFF)
promeki_config_option(PROMEKI_BUILD_TESTS       ON)
promeki_config_option(PROMEKI_BUILD_DOCS        OFF)
promeki_config_option(PROMEKI_BUILD_BENCHMARKS  OFF)
promeki_config_option(PROMEKI_BUILD_STATS       OFF)

# ---------------------------------------------------------------------------
# Build hygiene
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_WARNINGS_AS_ERRORS OFF)
promeki_config_option(PROMEKI_USE_PCH            ON)
promeki_config_option(PROMEKI_USE_CCACHE         ON)

# ---------------------------------------------------------------------------
# Feature flags — pro A/V core only.  No networking, no software codecs,
# no fonts, no audio file I/O or resampling.
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_ENABLE_NETWORK   OFF)
promeki_config_option(PROMEKI_ENABLE_HTTP      OFF)
promeki_config_option(PROMEKI_ENABLE_TLS       OFF)
promeki_config_option(PROMEKI_ENABLE_SRT       OFF)
promeki_config_option(PROMEKI_ENABLE_PROAV     ON)
promeki_config_option(PROMEKI_ENABLE_MUSIC     OFF)
promeki_config_option(PROMEKI_ENABLE_PNG       OFF)
promeki_config_option(PROMEKI_ENABLE_JPEG      OFF)
promeki_config_option(PROMEKI_ENABLE_JPEGXS    OFF)
# x264 is a GPL software H.264 encoder — off for this "PROAV on, codecs
# off" preset.  Keeps the build non-GPL and preserves the codecs-off link
# coverage (NVENC covers H.264 on GPU appliances when needed).
promeki_config_option(PROMEKI_ENABLE_X264      OFF)
promeki_config_option(PROMEKI_ENABLE_FREETYPE  OFF)
promeki_config_option(PROMEKI_ENABLE_AUDIO     OFF)
# FLAC / Vorbis / MP3 ride on libsndfile (AUDIO) — off when AUDIO is off.
promeki_config_option(PROMEKI_ENABLE_FLAC      OFF)
promeki_config_option(PROMEKI_ENABLE_VORBIS    OFF)
promeki_config_option(PROMEKI_ENABLE_MP3       OFF)
promeki_config_option(PROMEKI_ENABLE_OPUS      OFF)
promeki_config_option(PROMEKI_ENABLE_AAC       OFF)
promeki_config_option(PROMEKI_ENABLE_SRC       OFF)
# CSC (pixel-format conversion, vendored Highway) is pure compute and has
# no external deps — keep it on so pro A/V pixel paths still work.
promeki_config_option(PROMEKI_ENABLE_CSC       ON)
# Resource filesystem (vendored cirf) is lightweight — keep it on.
promeki_config_option(PROMEKI_ENABLE_CIRF      ON)
# SDI capture / playout backends.  NTV2 is fully vendored (needs libudev),
# V4L2 needs Linux + ALSA.  Both are left probe-driven so the preset builds
# anywhere; pin one ON in a derived config when targeting a known box:
#
#   include(${CMAKE_CURRENT_LIST_DIR}/proav-embedded.cmake)
#   promeki_config_option(PROMEKI_ENABLE_NTV2 ON)
#
# promeki_config_option(PROMEKI_ENABLE_NTV2    ON)
# promeki_config_option(PROMEKI_ENABLE_V4L2    ON)
# memfd / CUDA / NVENC / NVDEC / NDI default ON when the host has them;
# leave probe-driven so the same config works with and without GPU / SDKs.
