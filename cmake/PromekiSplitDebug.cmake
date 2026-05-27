# PromekiSplitDebug.cmake — separate debug symbols from shared libraries.
#
# Defines promeki_split_debug(<target>), a helper that, as a POST_BUILD step,
# extracts a shared library's DWARF debug info into a standalone `.debug`
# file and strips it out of the shipped `.so`.  The two files are linked by a
# `.gnu_debuglink` section (basename + CRC) so a debugger re-associates them
# automatically once the `.debug` file sits beside the library.
#
# Why: with `-g`/`-g1` the debug info dwarfs the executable code — for the
# devrelease / debug builds we run day-to-day, that bloat is dead weight,
# because we only crack open a debugger occasionally.  Splitting lets us:
#   * keep build/bin + build/lib as a lean, copyable runtime bundle that
#     carries no debug info, and
#   * pull the symbols back in on demand (see "Re-attaching" below) when we
#     actually need to debug — no rebuild required.
#
# `--strip-debug` (not `--strip-all`) is deliberate: it removes the DWARF
# debug sections but KEEPS the `.symtab` symbol table, so backtraces still
# symbolicate to function names without the heavy type/line tables.  The
# build-id note (emitted by GNU ld by default) is preserved either way.
#
# The `.debug` files are written to PROMEKI_DEBUG_OUTPUT_DIRECTORY
# (build/lib-debug), a sibling of build/lib — never into build/lib itself —
# so the runtime bundle stays free of symbols.
#
# Re-attaching the symbols for a debug session, either:
#   * copy them next to the libraries (debuglink basename match — zero gdb
#     config):   cp build/lib-debug/*.debug build/lib/
#   * or point the debugger at the split-debug tree by build-id:
#       (gdb) set debug-file-directory build/lib-debug
#     (works when the build-id .build-id/ layout is used; the flat layout
#     here relies on the copy form above).
#
# No-op unless PROMEKI_SPLIT_DEBUG is ON, the toolchain is ELF
# (UNIX AND NOT APPLE — Mach-O uses dSYM bundles, a different mechanism),
# objcopy is available, and the active build type actually carries debug
# info (Debug / DevRelease / RelWithDebInfo).  Release / MinSizeRel compile
# without `-g`, so there is nothing to split.

function(promeki_split_debug tgt)
    # PROMEKI_SPLIT_DEBUG_ACTIVE is the single gate (computed once in the
    # top-level CMakeLists): split enabled, ELF toolchain, objcopy present,
    # and a debug-info-bearing build type.
    if(NOT PROMEKI_SPLIT_DEBUG_ACTIVE)
        return()
    endif()

    set(_libfile "$<TARGET_FILE:${tgt}>")
    set(_dbgfile "${PROMEKI_DEBUG_OUTPUT_DIRECTORY}/$<TARGET_FILE_NAME:${tgt}>.debug")

    add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROMEKI_DEBUG_OUTPUT_DIRECTORY}"
        # 1. Copy the DWARF debug info out into its own file.
        COMMAND "${CMAKE_OBJCOPY}" --only-keep-debug "${_libfile}" "${_dbgfile}"
        # 2. Strip the DWARF (but keep .symtab) from the shipped library.
        COMMAND "${CMAKE_OBJCOPY}" --strip-debug "${_libfile}"
        # 3. Record a basename+CRC link from the library back to its .debug.
        # NOTE: no quotes around ${_dbgfile} here — it is part of a single
        # `--add-gnu-debuglink=<path>` token, and VERBATIM already quotes the
        # whole argument; embedding quotes would feed them to objcopy as part
        # of the filename.
        COMMAND "${CMAKE_OBJCOPY}" --add-gnu-debuglink=${_dbgfile} "${_libfile}"
        COMMENT "Splitting debug info: ${tgt} -> ${PROMEKI_DEBUG_OUTPUT_DIRECTORY}"
        VERBATIM)
endfunction()
