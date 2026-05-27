# PromekiStripSo.cmake — strip a single shared object, splitting its
# symbols into a sibling .debug file.
#
# Defines promeki_strip_so(<so> <debug_dir> <objcopy> <strip_flag>), the
# script-mode counterpart of promeki_split_debug() (PromekiSplitDebug.cmake).
# It is meant to be include()'d from `cmake -P` helper scripts that produce
# or stage shared objects (the vendored-dep stager, the SRT bundler), so the
# objcopy dance lives in exactly one place.
#
#   so          path of the shared object to strip (modified in place)
#   debug_dir   directory the <name>.debug file is written into
#   objcopy     path to objcopy; empty/false -> do nothing (caller opted out)
#   strip_flag  which strip to apply.  Vendored / third-party libraries we
#               never debug into use --strip-unneeded (drops .symtab, keeps
#               the .dynsym a shared object needs to stay loadable+linkable),
#               which actually shrinks them — those builds carry a .symtab
#               but no DWARF.  --strip-debug is the gentler form (keeps
#               .symtab) used for our own libraries elsewhere.
#
# --only-keep-debug captures whatever symbol/debug content exists (.symtab
# and any .debug_*) into the .debug file first, so the strip is reversible:
# drop the .debug next to the library and a debugger re-associates it via
# the .gnu_debuglink (basename + CRC) recorded in the stripped object.

function(promeki_strip_so so debug_dir objcopy strip_flag)
    if(NOT objcopy)
        return()
    endif()
    if(NOT EXISTS "${so}")
        message(WARNING "promeki_strip_so: ${so} does not exist; skipping")
        return()
    endif()

    get_filename_component(_name "${so}" NAME)
    set(_dbg "${debug_dir}/${_name}.debug")
    file(MAKE_DIRECTORY "${debug_dir}")

    execute_process(
        COMMAND "${objcopy}" --only-keep-debug "${so}" "${_dbg}"
        RESULT_VARIABLE _rc ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "promeki_strip_so: --only-keep-debug ${so} failed:\n${_err}")
    endif()
    execute_process(
        COMMAND "${objcopy}" ${strip_flag} "${so}"
        RESULT_VARIABLE _rc ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "promeki_strip_so: ${strip_flag} ${so} failed:\n${_err}")
    endif()
    # No quotes around ${_dbg}: it is part of a single --add-gnu-debuglink=<p>
    # token (see PromekiSplitDebug.cmake for the same caveat).
    execute_process(
        COMMAND "${objcopy}" --add-gnu-debuglink=${_dbg} "${so}"
        RESULT_VARIABLE _rc ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "promeki_strip_so: --add-gnu-debuglink ${so} failed:\n${_err}")
    endif()
endfunction()
