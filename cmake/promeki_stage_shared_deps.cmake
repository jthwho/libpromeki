# promeki_stage_shared_deps.cmake
#
# Helper invoked at build time (via cmake -P) to stage the vendored
# shared dependencies that ship beside libpromeki.so into the build
# tree's library output dir, so build/lib is a faithful mirror of the
# installed lib/ and apps run straight out of build/bin via $ORIGIN
# without reaching into the thirdparty-install prefix.
#
# The real-file -> SONAME -> linker-name symlink chain is preserved:
# symlinks are recreated as symlinks (not dereferenced into duplicate
# real files), matching what install(DIRECTORY) ships.
#
# Required vars (set by the caller via -D...):
#   SRC       source dir (the thirdparty-install lib dir)
#   DST       destination dir (the build library output dir)
#   PATTERNS  ';'-separated list of globs, e.g. "libsndfile.so*;libopus.so*"
#
# Optional vars (split-debug; both must be set to strip):
#   OBJCOPY   path to objcopy; when set, each staged real .so is stripped
#             (--strip-unneeded) and its symbols saved to DEBUGDIR
#   DEBUGDIR  directory the <name>.debug files are written into
#
# The originals in SRC are left pristine — we re-copy and strip into DST on
# every run, so the captured .debug stays complete regardless of how many
# times staging re-runs (a strip in place at the source would, on the next
# build, extract an already-empty debug section over the good one).

foreach(_var SRC DST PATTERNS)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "promeki_stage_shared_deps.cmake: ${_var} is not set")
    endif()
endforeach()

include(${CMAKE_CURRENT_LIST_DIR}/PromekiStripSo.cmake)

file(MAKE_DIRECTORY "${DST}")

foreach(_pat ${PATTERNS})
    file(GLOB _matches "${SRC}/${_pat}")
    foreach(_f ${_matches})
        get_filename_component(_name "${_f}" NAME)
        set(_dest "${DST}/${_name}")
        if(IS_SYMLINK "${_f}")
            # Recreate the link verbatim (its target is a sibling name).
            file(READ_SYMLINK "${_f}" _target)
            file(REMOVE "${_dest}")
            file(CREATE_LINK "${_target}" "${_dest}" SYMBOLIC)
        else()
            file(COPY "${_f}" DESTINATION "${DST}")
            # Strip the freshly-staged real object (symlinks point at it).
            # --strip-unneeded: these vendored libs carry a .symtab but no
            # DWARF, so this is what actually shrinks them while keeping the
            # .dynsym they need to stay loadable + linkable.
            promeki_strip_so("${_dest}" "${DEBUGDIR}" "${OBJCOPY}" "--strip-unneeded")
        endif()
    endforeach()
endforeach()
