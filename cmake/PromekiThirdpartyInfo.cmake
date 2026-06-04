# PromekiThirdpartyInfo.cmake
#
# Defines a `thirdparty-info` custom target that prints a report of every
# vendored thirdparty/* git submodule:
#
#   * dependency  — the submodule name
#   * used        — whether the current configuration actually builds it
#                   (mirrors the PROMEKI_ENABLE_* / PROMEKI_USE_SYSTEM_*
#                   gating in _promeki_submodule_required())
#   * pulled      — whether the submodule has been checked out (live check)
#   * mirrored    — whether a mirror override rewrote the fetch URL
#   * fetch URL   — the URL the build system fetches the submodule from:
#                   the mirror URL when a mirror is configured for it,
#                   otherwise the upstream URL from .gitmodules
#
# "used" and "mirrored"/"fetch URL" are fixed by the current configure
# (feature flags + the discovered mirrors file); "pulled" is re-evaluated
# every time the target runs, so it reflects the live working tree.
#
# Requires cmake/PromekiSubmodules.cmake to have been included first (it
# reuses _promeki_find_mirrors_file() and _promeki_submodule_required()).
#
# Caller workflow:
#     include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PromekiSubmodules.cmake)
#     promeki_init_submodules()
#     include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PromekiThirdpartyInfo.cmake)
#     promeki_add_thirdparty_info_target()

# Parse .gitmodules into parallel path + url lists.  Each [submodule] block
# lists `path` before `url`, so we pair a pending path with the next url.
function(_promeki_tpinfo_parse_gitmodules out_paths out_urls)
    set(_paths "")
    set(_urls "")
    set(_gitmodules ${CMAKE_CURRENT_SOURCE_DIR}/.gitmodules)
    if(NOT EXISTS ${_gitmodules})
        set(${out_paths} "" PARENT_SCOPE)
        set(${out_urls} "" PARENT_SCOPE)
        return()
    endif()
    file(STRINGS ${_gitmodules} _lines)
    set(_cur_path "")
    foreach(_line IN LISTS _lines)
        if(_line MATCHES "^[ \t]*path[ \t]*=[ \t]*(.+)$")
            string(STRIP "${CMAKE_MATCH_1}" _cur_path)
        elseif(_line MATCHES "^[ \t]*url[ \t]*=[ \t]*(.+)$")
            string(STRIP "${CMAKE_MATCH_1}" _u)
            if(_cur_path)
                list(APPEND _paths "${_cur_path}")
                list(APPEND _urls "${_u}")
                set(_cur_path "")
            endif()
        endif()
    endforeach()
    set(${out_paths} "${_paths}" PARENT_SCOPE)
    set(${out_urls} "${_urls}" PARENT_SCOPE)
endfunction()

# Resolve the effective fetch URL for <_upstream> against the flat
# (upstream, mirror) <_pairs> list, replicating git's `insteadOf`
# longest-prefix-wins rewrite.  Sets <out_url> to the rewritten URL (or the
# upstream unchanged) and <out_mirrored> to TRUE/FALSE.
function(_promeki_tpinfo_resolve_url _upstream _pairs out_url out_mirrored)
    set(_best_len -1)
    set(_result "${_upstream}")
    set(_mirrored FALSE)
    list(LENGTH _pairs _n)
    if(_n GREATER 1)
        math(EXPR _stop "${_n} - 2")
        foreach(_i RANGE 0 ${_stop} 2)
            math(EXPR _j "${_i} + 1")
            list(GET _pairs ${_i} _ups)
            list(GET _pairs ${_j} _mir)
            string(LENGTH "${_ups}" _len)
            string(FIND "${_upstream}" "${_ups}" _pos)
            if(_pos EQUAL 0 AND _len GREATER _best_len)
                set(_best_len ${_len})
                string(SUBSTRING "${_upstream}" ${_len} -1 _tail)
                set(_result "${_mir}${_tail}")
                set(_mirrored TRUE)
            endif()
        endforeach()
    endif()
    set(${out_url} "${_result}" PARENT_SCOPE)
    set(${out_mirrored} ${_mirrored} PARENT_SCOPE)
endfunction()

# Static body of the generated run-script: helper + row-emitter that does
# the live "pulled" check.  Written verbatim (bracket literal suppresses
# expansion; ${...} refs are evaluated when the script runs via `cmake -P`).
set(_PROMEKI_TPINFO_DEFS [==[
function(_promeki_tpinfo_pad _str _width _out)
    string(LENGTH "${_str}" _len)
    set(_s "${_str}")
    while(_len LESS _width)
        string(APPEND _s " ")
        math(EXPR _len "${_len} + 1")
    endwhile()
    set(${_out} "${_s}" PARENT_SCOPE)
endfunction()

function(_emit _name _path _used _mirrored _furl)
    set(_pulled "no")
    if(EXISTS "${_srcdir}/${_path}/.git")
        set(_pulled "yes")
    else()
        file(GLOB _contents "${_srcdir}/${_path}/*")
        if(_contents)
            set(_pulled "yes")
        endif()
    endif()
    # Live version/commit: nearest tag + offset, else short SHA, plus a
    # "-dirty" suffix when the submodule has local modifications.
    set(_ver "-")
    if(_pulled AND _git)
        execute_process(
            COMMAND "${_git}" -C "${_srcdir}/${_path}"
                describe --tags --always --dirty --abbrev=12
            OUTPUT_VARIABLE _desc
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_desc)
            set(_ver "${_desc}")
        endif()
    endif()
    _promeki_tpinfo_pad("${_name}"     18 _c1)
    _promeki_tpinfo_pad("${_used}"      6 _c2)
    _promeki_tpinfo_pad("${_pulled}"    8 _c3)
    _promeki_tpinfo_pad("${_mirrored}" 10 _c4)
    _promeki_tpinfo_pad("${_ver}"      26 _c5)
    message("${_c1}${_c2}${_c3}${_c4}${_c5}${_furl}")
endfunction()
]==])

set(_PROMEKI_TPINFO_HEADER [==[
message("")
message("Vendored thirdparty dependencies")
message("  source tree : ${_srcdir}")
message("  mirrors file: ${_mirrors_file}")
message("")
_promeki_tpinfo_pad("dependency" 18 _h1)
_promeki_tpinfo_pad("used"        6 _h2)
_promeki_tpinfo_pad("pulled"      8 _h3)
_promeki_tpinfo_pad("mirrored"   10 _h4)
_promeki_tpinfo_pad("version"    26 _h5)
message("${_h1}${_h2}${_h3}${_h4}${_h5}fetch URL")
message("------------------------------------------------------------------------------------------------------------")
]==])

set(_PROMEKI_TPINFO_FOOTER [==[
message("")
message("used     = built by this configuration's feature flags")
message("pulled   = submodule checked out in the working tree (live)")
message("mirrored = fetch URL rewritten by the discovered mirrors file")
message("version  = git describe of the checked-out submodule (live)")
message("")
]==])

# Entry point: build the per-dependency data at configure time and emit a
# `cmake -P` script driving the `thirdparty-info` target.
function(promeki_add_thirdparty_info_target)
    # Discover & load the same mirror map the submodule auto-init uses.
    _promeki_find_mirrors_file()
    set(_pairs "")
    set(_mf_display "(none — using upstream URLs)")
    if(PROMEKI_MIRRORS_FILE AND EXISTS "${PROMEKI_MIRRORS_FILE}")
        set(_mf_display "${PROMEKI_MIRRORS_FILE}")
        unset(PROMEKI_MIRRORS)
        include(${PROMEKI_MIRRORS_FILE})
        if(DEFINED PROMEKI_MIRRORS)
            set(_pairs "${PROMEKI_MIRRORS}")
        endif()
    endif()

    _promeki_tpinfo_parse_gitmodules(_paths _urls)

    # Git is used by the run-script for the live version/commit column.
    find_package(Git QUIET)
    set(_git_exe "")
    if(GIT_FOUND)
        set(_git_exe "${GIT_EXECUTABLE}")
    endif()

    # Assemble the generated script: runtime vars, static defs/header, one
    # _emit() call per dependency, static footer.
    set(_gen "")
    string(APPEND _gen "# Generated by PromekiThirdpartyInfo.cmake — do not edit.\n")
    string(APPEND _gen "set(_srcdir \"${CMAKE_CURRENT_SOURCE_DIR}\")\n")
    string(APPEND _gen "set(_mirrors_file \"${_mf_display}\")\n")
    string(APPEND _gen "set(_git \"${_git_exe}\")\n")
    string(APPEND _gen "${_PROMEKI_TPINFO_DEFS}")
    string(APPEND _gen "${_PROMEKI_TPINFO_HEADER}")

    list(LENGTH _paths _np)
    if(_np GREATER 0)
        math(EXPR _last "${_np} - 1")
        foreach(_i RANGE 0 ${_last})
            list(GET _paths ${_i} _path)
            list(GET _urls ${_i} _ups)
            get_filename_component(_name "${_path}" NAME)
            _promeki_submodule_required("${_path}" _req)
            _promeki_tpinfo_resolve_url("${_ups}" "${_pairs}" _furl _mir)
            if(_req)
                set(_reqs "yes")
            else()
                set(_reqs "no")
            endif()
            if(_mir)
                set(_mirs "yes")
            else()
                set(_mirs "no")
            endif()
            string(APPEND _gen
                "_emit(\"${_name}\" \"${_path}\" \"${_reqs}\" \"${_mirs}\" \"${_furl}\")\n")
        endforeach()
    endif()

    string(APPEND _gen "${_PROMEKI_TPINFO_FOOTER}")

    set(_script "${CMAKE_BINARY_DIR}/promeki-thirdparty-info.cmake")
    file(WRITE "${_script}" "${_gen}")

    add_custom_target(thirdparty-info
        COMMAND ${CMAKE_COMMAND} -P "${_script}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Vendored thirdparty dependency report"
        VERBATIM USES_TERMINAL)
endfunction()
