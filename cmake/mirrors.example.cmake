# Example mirror configuration shared by:
#
#   1. CMake submodule auto-init  (cmake/PromekiSubmodules.cmake)
#   2. scripts/mirror-thirdparty.py
#
# A single file feeds both directions: CMake uses PROMEKI_MIRRORS to
# rewrite submodule fetches at build time (via git's insteadOf); the
# mirror script uses the same map to find the push target for each
# submodule when populating / refreshing your self-hosted GitLab.
#
# Where to put it
# ---------------
# Both tools auto-discover a config file by walking the same search
# list (first hit wins).  Copy this file to any of these locations:
#
#   1. Set $PROMEKI_MIRRORS_FILE in your environment.
#   2. <repo-root>/mirrors.cmake                              (gitignored)
#   3. Per-user config:
#        Linux:   $XDG_CONFIG_HOME/promeki/mirrors.cmake
#                   (defaulting to ~/.config/promeki/mirrors.cmake)
#        macOS:   ~/Library/Application Support/promeki/mirrors.cmake
#                   (or ~/.config/promeki/mirrors.cmake — XDG fallback)
#        Windows: %APPDATA%\promeki\mirrors.cmake
#   4. System-wide:
#        Linux:   /etc/promeki/mirrors.cmake
#        macOS:   /Library/Application Support/promeki/mirrors.cmake
#                   (or /etc/promeki/mirrors.cmake)
#        Windows: %PROGRAMDATA%\promeki\mirrors.cmake
#
# Or override discovery explicitly:
#   CMake:  -DPROMEKI_MIRRORS_FILE=/path/to/your-mirrors.cmake
#   Script: --config /path/to/your-mirrors.cmake
#
# --- Variables -----------------------------------------------------------
#
# PROMEKI_MIRROR_API   (mirror script only — CMake ignores it)
#   API base URL of your GitLab server.  Used for project existence
#   checks and auto-creation.
#
# PROMEKI_MIRRORS      (both)
#   Flat list of (upstream-url, mirror-url) pairs.  The mirror script
#   matches each submodule's .gitmodules upstream URL EXACTLY against
#   this map; entries with no exact match are ignored by the script.
#
#   CMake forwards every pair to git as `-c url.<mirror>.insteadOf=<upstream>`,
#   so longest-match wins and you can layer per-repo overrides on top of
#   blanket prefix rewrites (e.g. https://github.com/ -> ssh://git@github.com/).
#
# The mirror URL implicitly encodes the project's namespace on the
# GitLab server — `ssh://git@host:port/GROUP/NAME.git` means the script
# will create / update the project at GROUP/NAME.

set(PROMEKI_MIRROR_API "https://gitlab.example.com")

# SSH push base, with optional non-standard port.  Adjust to match your
# server's SSH configuration.
set(BASE "ssh://git@gitlab.example.com:22")

set(PROMEKI_MIRRORS
    # Per-repo entries — each one tells the mirror script where to push
    # AND tells CMake how to rewrite the fetch URL.
    "https://github.com/libjpeg-turbo/libjpeg-turbo.git"
        "${BASE}/thirdparty/libjpeg-turbo.git"
    "https://github.com/nlohmann/json.git"
        "${BASE}/thirdparty/nlohmann-json.git"
    "https://github.com/libsndfile/libsndfile.git"
        "${BASE}/thirdparty/libsndfile.git"
    "https://gitlab.freedesktop.org/freetype/freetype.git"
        "${BASE}/thirdparty/freetype.git"
    "https://github.com/jthwho/libvtc.git"
        "${BASE}/sw/libvtc.git"
    "https://github.com/jthwho/cirf.git"
        "${BASE}/sw/cirf.git"

    # Blanket prefix rewrite — used only by CMake (the mirror script
    # ignores entries that don't exactly match a submodule's upstream
    # URL).  Handy for switching everything to SSH:
    "https://github.com/"
        "ssh://git@github.com/"
)
