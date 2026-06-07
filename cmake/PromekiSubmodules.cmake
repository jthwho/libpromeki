# PromekiSubmodules.cmake
#
# Auto-initialize the thirdparty/* git submodules that this configuration
# actually needs.  Each submodule is mapped to the CMake condition under
# which it's required (typically a PROMEKI_ENABLE_* feature flag plus a
# PROMEKI_USE_SYSTEM_* opt-out).  Submodules that aren't required are
# left untouched; required submodules whose checkout is empty are pulled
# via `git submodule update --init --recursive`.
#
# Mirror overrides
# ----------------
# Pass -DPROMEKI_MIRRORS_FILE=<path> to point at a CMake-includable file
# that sets a list variable named PROMEKI_MIRRORS, formatted as a flat
# sequence of (upstream-url, mirror-url) pairs:
#
#     set(PROMEKI_MIRRORS
#         "https://github.com/nlohmann/json.git"
#             "ssh://git@example.com:9122/thirdparty/nlohmann-json.git"
#         "https://github.com/libsndfile/libsndfile.git"
#             "ssh://git@example.com:9122/thirdparty/libsndfile.git"
#         "https://github.com/"
#             "ssh://git@github.com/"           # blanket prefix rewrite
#     )
#
# Each pair is forwarded to git as `-c url.<mirror>.insteadOf=<upstream>`,
# so git's longest-match rule applies: a full-URL entry overrides a prefix
# rewrite for the same repo.
#
# Caller workflow:
#     include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PromekiSubmodules.cmake)
#     promeki_init_submodules()
#
# Must run after every PROMEKI_ENABLE_* / PROMEKI_USE_SYSTEM_* option is
# defined, but before any `add_subdirectory(thirdparty/...)` or
# `ExternalProject_Add(... SOURCE_DIR thirdparty/...)` reference.

set(PROMEKI_MIRRORS_FILE "" CACHE FILEPATH
    "Optional path to a CMake-includable file that sets PROMEKI_MIRRORS for submodule URL rewrites")

# Well-known mirror-config search list (consulted only when
# PROMEKI_MIRRORS_FILE is empty).  Kept in lockstep with the matching
# `well_known_config_paths()` in scripts/mirror-thirdparty.py so a single
# file drives both CMake's submodule fetches and the mirror push script.
#
# Search order (first hit wins):
#   1. $PROMEKI_MIRRORS_FILE              (env var; cache override beats this)
#   2. <repo>/mirrors.cmake               (gitignored, repo-local)
#   3. Per-user config dir:
#        Linux:   $XDG_CONFIG_HOME/promeki/mirrors.cmake
#                   (defaulting to $HOME/.config/promeki/mirrors.cmake)
#        macOS:   $HOME/Library/Application Support/promeki/mirrors.cmake
#                   $HOME/.config/promeki/mirrors.cmake          (XDG fallback)
#        Windows: %APPDATA%/promeki/mirrors.cmake
#   4. System-wide config:
#        Linux:   /etc/promeki/mirrors.cmake
#        macOS:   /Library/Application Support/promeki/mirrors.cmake
#                   /etc/promeki/mirrors.cmake
#        Windows: %PROGRAMDATA%/promeki/mirrors.cmake
function(_promeki_find_mirrors_file)
    if(PROMEKI_MIRRORS_FILE)
        return()
    endif()

    set(_candidates "")

    if(DEFINED ENV{PROMEKI_MIRRORS_FILE} AND NOT "$ENV{PROMEKI_MIRRORS_FILE}" STREQUAL "")
        list(APPEND _candidates "$ENV{PROMEKI_MIRRORS_FILE}")
    endif()

    list(APPEND _candidates "${CMAKE_CURRENT_SOURCE_DIR}/mirrors.cmake")

    if(WIN32)
        if(DEFINED ENV{APPDATA} AND NOT "$ENV{APPDATA}" STREQUAL "")
            list(APPEND _candidates "$ENV{APPDATA}/promeki/mirrors.cmake")
        endif()
        if(DEFINED ENV{PROGRAMDATA} AND NOT "$ENV{PROGRAMDATA}" STREQUAL "")
            list(APPEND _candidates "$ENV{PROGRAMDATA}/promeki/mirrors.cmake")
        endif()
    elseif(APPLE)
        if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
            list(APPEND _candidates
                "$ENV{HOME}/Library/Application Support/promeki/mirrors.cmake"
                "$ENV{HOME}/.config/promeki/mirrors.cmake")
        endif()
        list(APPEND _candidates
            "/Library/Application Support/promeki/mirrors.cmake"
            "/etc/promeki/mirrors.cmake")
    else()
        if(DEFINED ENV{XDG_CONFIG_HOME} AND NOT "$ENV{XDG_CONFIG_HOME}" STREQUAL "")
            list(APPEND _candidates "$ENV{XDG_CONFIG_HOME}/promeki/mirrors.cmake")
        elseif(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
            list(APPEND _candidates "$ENV{HOME}/.config/promeki/mirrors.cmake")
        endif()
        list(APPEND _candidates "/etc/promeki/mirrors.cmake")
    endif()

    foreach(_cand IN LISTS _candidates)
        if(EXISTS "${_cand}")
            set(PROMEKI_MIRRORS_FILE "${_cand}"
                CACHE FILEPATH
                "Optional path to a CMake-includable file that sets PROMEKI_MIRRORS for submodule URL rewrites"
                FORCE)
            message(STATUS "PromekiSubmodules: discovered mirrors file at ${_cand}")
            return()
        endif()
    endforeach()
endfunction()

# Parses .gitmodules and returns the list of submodule paths in <out_var>.
# The list of paths is the source of truth — keeping a hand-maintained
# duplicate alongside it was the previous design and drifted.
function(_promeki_parse_gitmodules out_var)
    set(_paths "")
    set(_gitmodules ${CMAKE_CURRENT_SOURCE_DIR}/.gitmodules)
    if(NOT EXISTS ${_gitmodules})
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    file(STRINGS ${_gitmodules} _lines)
    foreach(_line IN LISTS _lines)
        # Match lines like:  `path = thirdparty/foo` (with optional
        # leading whitespace).  CMake regex doesn't have \s; use a
        # bracket class.
        if(_line MATCHES "^[ \t]*path[ \t]*=[ \t]*(.+)$")
            string(STRIP "${CMAKE_MATCH_1}" _stripped)
            list(APPEND _paths "${_stripped}")
        endif()
    endforeach()
    set(${out_var} "${_paths}" PARENT_SCOPE)
endfunction()

# Sets <out_var> to TRUE if <_path> is needed for the current configuration.
#
# Submodules listed in .gitmodules but not handled here are treated as
# "always required" (covers unconditional vendored deps that don't
# need a feature gate — e.g. nlohmann-json, pugixml, libvtc — and is
# also the safe default if a new submodule is added without updating
# this function).
function(_promeki_submodule_required _path out_var)
    set(_req FALSE)
    if(${_path} STREQUAL "thirdparty/nlohmann-json")
        if(NOT PROMEKI_USE_SYSTEM_NLOHMANN_JSON)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/pugixml")
        if(NOT PROMEKI_USE_SYSTEM_PUGIXML)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/libvtc")
        if(NOT PROMEKI_USE_SYSTEM_VTC)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/cirf")
        if(PROMEKI_ENABLE_CIRF AND NOT PROMEKI_USE_SYSTEM_CIRF)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/llhttp")
        if(PROMEKI_ENABLE_NETWORK AND PROMEKI_ENABLE_HTTP AND NOT PROMEKI_USE_SYSTEM_LLHTTP)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/mbedtls")
        if(PROMEKI_ENABLE_NETWORK AND PROMEKI_ENABLE_TLS AND NOT PROMEKI_USE_SYSTEM_MBEDTLS)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/srt")
        if(PROMEKI_ENABLE_NETWORK AND PROMEKI_ENABLE_SRT AND NOT PROMEKI_USE_SYSTEM_SRT)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/srt-mbedtls")
        # Paired with the vendored SRT build; never used standalone.
        if(PROMEKI_ENABLE_NETWORK AND PROMEKI_ENABLE_SRT AND NOT PROMEKI_USE_SYSTEM_SRT)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/mdns")
        if(PROMEKI_ENABLE_NETWORK AND PROMEKI_ENABLE_MDNS AND NOT PROMEKI_USE_SYSTEM_MDNS)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/highway")
        if(PROMEKI_ENABLE_CSC AND NOT PROMEKI_USE_SYSTEM_HIGHWAY)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/zlib-ng")
        # Consumed by libspng and freetype.
        if((PROMEKI_ENABLE_PNG OR PROMEKI_ENABLE_FREETYPE) AND NOT PROMEKI_USE_SYSTEM_ZLIB)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/libjpeg-turbo")
        if(PROMEKI_ENABLE_JPEG AND NOT PROMEKI_USE_SYSTEM_LIBJPEG)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/svt-jpeg-xs")
        if(PROMEKI_ENABLE_JPEGXS AND NOT PROMEKI_USE_SYSTEM_SVT_JPEG_XS)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/libspng")
        if(PROMEKI_ENABLE_PNG AND NOT PROMEKI_USE_SYSTEM_LIBSPNG)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/libsndfile")
        if(PROMEKI_ENABLE_AUDIO AND NOT PROMEKI_USE_SYSTEM_SNDFILE)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/libogg")
        # libogg is the Ogg container layer used by FLAC (Ogg-FLAC) and
        # Vorbis.  Required whenever either codec is on and we're not
        # using a system copy.
        if((PROMEKI_ENABLE_FLAC OR PROMEKI_ENABLE_VORBIS) AND NOT PROMEKI_USE_SYSTEM_OGG)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/libflac")
        if(PROMEKI_ENABLE_FLAC AND NOT PROMEKI_USE_SYSTEM_FLAC)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/libvorbis")
        if(PROMEKI_ENABLE_VORBIS AND NOT PROMEKI_USE_SYSTEM_VORBIS)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/mpg123")
        if(PROMEKI_ENABLE_MP3 AND NOT PROMEKI_USE_SYSTEM_MPG123)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/lame")
        if(PROMEKI_ENABLE_MP3 AND NOT PROMEKI_USE_SYSTEM_LAME)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/opus")
        if(PROMEKI_ENABLE_OPUS AND NOT PROMEKI_USE_SYSTEM_OPUS)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/fdk-aac")
        if(PROMEKI_ENABLE_AAC AND NOT PROMEKI_USE_SYSTEM_FDKAAC)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/ffmpeg")
        if(PROMEKI_ENABLE_FFMPEG AND NOT PROMEKI_USE_SYSTEM_FFMPEG)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/libsamplerate")
        if(PROMEKI_ENABLE_SRC AND NOT PROMEKI_USE_SYSTEM_SAMPLERATE)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/freetype")
        if(PROMEKI_ENABLE_FREETYPE AND NOT PROMEKI_USE_SYSTEM_FREETYPE)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/libajantv2")
        if(PROMEKI_ENABLE_NTV2 AND NOT PROMEKI_USE_SYSTEM_NTV2)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/x264")
        if(PROMEKI_ENABLE_X264 AND NOT PROMEKI_USE_SYSTEM_X264)
            set(_req TRUE)
        endif()
    elseif(${_path} STREQUAL "thirdparty/whisper.cpp")
        if(PROMEKI_ENABLE_WHISPER AND NOT PROMEKI_USE_SYSTEM_WHISPER)
            set(_req TRUE)
        endif()
    else()
        # Unknown path: default to "required" so a newly-added
        # submodule that hasn't been wired up to a feature gate
        # still gets fetched.  A configure-time message makes the
        # situation visible so the omission gets noticed.
        message(STATUS
            "PromekiSubmodules: ${_path} has no feature gate in "
            "_promeki_submodule_required(); treating as required")
        set(_req TRUE)
    endif()
    set(${out_var} ${_req} PARENT_SCOPE)
endfunction()

# Sets <out_var> to TRUE if the submodule under <_path> has been checked out.
function(_promeki_submodule_initialized _path out_var)
    set(_have FALSE)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_path}/.git)
        set(_have TRUE)
    else()
        # Fallback for trees populated outside of `git submodule update`
        # (e.g. tarball drops): a non-empty directory counts as present.
        file(GLOB _contents ${CMAKE_CURRENT_SOURCE_DIR}/${_path}/*)
        if(_contents)
            set(_have TRUE)
        endif()
    endif()
    set(${out_var} ${_have} PARENT_SCOPE)
endfunction()

# Parses PROMEKI_MIRRORS_FILE (if set) and converts it into a list of git
# `-c url.<mirror>.insteadOf=<upstream>` arguments.
function(_promeki_load_mirrors out_var)
    set(_args "")
    if(PROMEKI_MIRRORS_FILE)
        if(NOT EXISTS ${PROMEKI_MIRRORS_FILE})
            message(FATAL_ERROR
                "PROMEKI_MIRRORS_FILE points at a file that does not exist: "
                "${PROMEKI_MIRRORS_FILE}")
        endif()
        unset(PROMEKI_MIRRORS)
        include(${PROMEKI_MIRRORS_FILE})
        if(NOT DEFINED PROMEKI_MIRRORS)
            message(WARNING
                "PROMEKI_MIRRORS_FILE=${PROMEKI_MIRRORS_FILE} did not set "
                "PROMEKI_MIRRORS; no mirror overrides will be applied")
        else()
            list(LENGTH PROMEKI_MIRRORS _n)
            math(EXPR _odd "${_n} % 2")
            if(NOT _odd EQUAL 0)
                message(FATAL_ERROR
                    "PROMEKI_MIRRORS in ${PROMEKI_MIRRORS_FILE} must contain "
                    "an even number of items (upstream/mirror pairs); got ${_n}")
            endif()
            if(_n GREATER 0)
                math(EXPR _stop "${_n} - 2")
                foreach(_i RANGE 0 ${_stop} 2)
                    math(EXPR _j "${_i} + 1")
                    list(GET PROMEKI_MIRRORS ${_i} _ups)
                    list(GET PROMEKI_MIRRORS ${_j} _mir)
                    list(APPEND _args -c "url.${_mir}.insteadOf=${_ups}")
                endforeach()
                math(EXPR _pairs "${_n} / 2")
                message(STATUS
                    "PromekiSubmodules: loaded ${_pairs} mirror override(s) "
                    "from ${PROMEKI_MIRRORS_FILE}")
            endif()
        endif()
    endif()
    set(${out_var} ${_args} PARENT_SCOPE)
endfunction()

# Entry point: walks every submodule listed in .gitmodules, initializes the
# ones needed by the current configuration that aren't already populated.
function(promeki_init_submodules)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
        # Source tarball or unpacked archive — no git metadata to drive
        # submodule init.  Assume the user (or upstream packager) staged
        # thirdparty/ contents themselves.
        return()
    endif()

    find_package(Git QUIET)
    if(NOT GIT_FOUND)
        message(STATUS
            "PromekiSubmodules: git executable not found; skipping submodule "
            "auto-init.  Required submodules must be populated manually.")
        return()
    endif()

    _promeki_find_mirrors_file()
    _promeki_load_mirrors(_mirror_args)
    _promeki_parse_gitmodules(_submodule_paths)

    set(_initialized "")
    foreach(_path ${_submodule_paths})
        _promeki_submodule_required(${_path} _need)
        if(NOT _need)
            continue()
        endif()
        _promeki_submodule_initialized(${_path} _have)
        if(_have)
            continue()
        endif()
        message(STATUS "PromekiSubmodules: initializing ${_path}")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} ${_mirror_args}
                submodule update --init --recursive -- ${_path}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            RESULT_VARIABLE _rc
            OUTPUT_VARIABLE _out
            ERROR_VARIABLE _err
        )
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR
                "Failed to initialize submodule ${_path} (exit ${_rc}).\n"
                "stdout:\n${_out}\n"
                "stderr:\n${_err}\n"
                "Hint: set -DPROMEKI_MIRRORS_FILE=<file> to use an internal "
                "mirror, or run\n"
                "    git submodule update --init --recursive -- ${_path}\n"
                "manually and reconfigure.")
        endif()
        list(APPEND _initialized ${_path})
    endforeach()

    if(_initialized)
        list(JOIN _initialized ", " _msg)
        message(STATUS "PromekiSubmodules: initialized ${_msg}")
    endif()
endfunction()
