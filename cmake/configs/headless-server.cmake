# Headless server preset — networking + pro A/V, no UI.
#
# Target use case: server-side ingest, transcode, mux, RTP / SRT / HTTP
# media plumbing.  Includes everything you need to push media bits over
# the wire, with no terminal UI, no SDL viewer, no demo apps, and no
# benchmark harness in the default `all` build.

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
# Feature flags — networking + pro A/V, no font / UI dependencies.
# ---------------------------------------------------------------------------
promeki_config_option(PROMEKI_ENABLE_NETWORK   ON)
promeki_config_option(PROMEKI_ENABLE_HTTP      ON)
promeki_config_option(PROMEKI_ENABLE_TLS       ON)
promeki_config_option(PROMEKI_ENABLE_SRT       ON)
promeki_config_option(PROMEKI_ENABLE_PROAV     ON)
promeki_config_option(PROMEKI_ENABLE_MUSIC     OFF)
promeki_config_option(PROMEKI_ENABLE_PNG       ON)
promeki_config_option(PROMEKI_ENABLE_JPEG      ON)
# JPEGXS is x86-only + needs nasm/yasm — leave probe-driven.
# promeki_config_option(PROMEKI_ENABLE_JPEGXS  ON)
promeki_config_option(PROMEKI_ENABLE_FREETYPE  OFF)
promeki_config_option(PROMEKI_ENABLE_AUDIO     ON)
promeki_config_option(PROMEKI_ENABLE_OPUS      ON)
promeki_config_option(PROMEKI_ENABLE_AAC       ON)
promeki_config_option(PROMEKI_ENABLE_SRC       ON)
promeki_config_option(PROMEKI_ENABLE_CSC       ON)
promeki_config_option(PROMEKI_ENABLE_CIRF      ON)
# NTV2 is fully vendored, so it builds anywhere libudev is available —
# pin ON for SDI ingest / playout boxes.
promeki_config_option(PROMEKI_ENABLE_NTV2      ON)
# V4L2 needs Linux + ALSA — leave probe-driven for portable builds, or
# pin ON in a derived config when targeting a known capture box.
# promeki_config_option(PROMEKI_ENABLE_V4L2    OFF)
# memfd / CUDA / NVENC / NVDEC / NDI default ON when the host has them;
# leave probe-driven so the same config works on machines with and
# without GPU / NDI SDK installs.
