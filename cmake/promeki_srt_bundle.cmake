# promeki_srt_bundle.cmake
#
# Helper script invoked at build time (via cmake -P) to link libsrt.a
# together with the three isolated mbedTLS-3.6 static archives into a
# single *shared* object — libpromeki_srt.so — in which:
#
#   * the public SRT C API (srt_*) stays GLOBAL in the dynamic symbol
#     table (SRT_API expands to visibility("default") on non-Windows, so
#     these survive even though SRT is compiled -fvisibility=hidden),
#   * every SRT-internal symbol is already hidden by the visibility
#     preset and so never reaches the dynsym,
#   * every mbedTLS-3.6 symbol is forced LOCAL by --exclude-libs naming
#     the three mbedTLS archives — invisible to anything that links
#     against the .so, so the 3.6 crypto stack cannot collide with the
#     *main* mbedTLS-4.x stack the rest of libpromeki.so links against.
#
# This replaces the older `ld -r` + `objcopy --localize-symbols` pass:
# --exclude-libs does the same symbol localization declaratively, at the
# moment the shared object's dynamic symbol table is produced, with no
# nm enumeration step.  mbedTLS-3.6 is statically absorbed into the .so
# (it never appears as a DT_NEEDED), so there is nothing for a consumer
# to resolve or accidentally bind to a different copy.
#
# Required vars (set by the parent CMakeLists.txt via -D...):
#   BUNDLE_SO         path of the shared object to produce
#   BUNDLE_SONAME     DT_SONAME to embed (e.g. libpromeki_srt.so)
#   SRT_LIBA          input libsrt.a (built by srt_ep)
#   MBEDTLS_LIBA      input libmbedtls.a    (3.6, built by srt_mbedtls_ep)
#   MBEDX509_LIBA     input libmbedx509.a   (3.6)
#   MBEDCRYPTO_LIBA   input libmbedcrypto.a (3.6)
#   CXX_TOOL          path to the C++ compiler driver (links libstdc++)
#
# Optional vars (split-debug; both must be set to strip):
#   OBJCOPY           path to objcopy; when set, the freshly-linked bundle is
#                     stripped (--strip-unneeded) and its symbols saved to
#                     DEBUGDIR.  The public srt_* API lives in .dynsym (kept);
#                     only .symtab is dropped.
#   DEBUGDIR          directory the <name>.debug file is written into

foreach(_var BUNDLE_SO BUNDLE_SONAME
             SRT_LIBA MBEDTLS_LIBA MBEDX509_LIBA MBEDCRYPTO_LIBA
             CXX_TOOL)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "promeki_srt_bundle.cmake: ${_var} is not set")
    endif()
endforeach()

# Make sure the output directory exists.
get_filename_component(_outdir "${BUNDLE_SO}" DIRECTORY)
file(MAKE_DIRECTORY "${_outdir}")

# --exclude-libs matches archives by base filename; collect the three
# mbedTLS-3.6 archive names (no path) into a colon-separated list.
foreach(_a MBEDTLS_LIBA MBEDX509_LIBA MBEDCRYPTO_LIBA)
    get_filename_component(_name "${${_a}}" NAME)
    list(APPEND _mbed_names "${_name}")
endforeach()
string(REPLACE ";" ":" _exclude_arg "${_mbed_names}")

# Link the shared object:
#   * --whole-archive on libsrt forces in every libsrt member (we want
#     the complete SRT library present in the .so),
#   * the mbedTLS archives follow and are pulled in by reference (only
#     the .o's haicrypt actually uses get dragged in),
#   * --exclude-libs localizes every symbol the mbedTLS archives define,
#   * -pthread because SRT references pthread / atomic even with
#     ENABLE_STDCXX_SYNC=ON (libstdc++ and haicrypt pull them in).
execute_process(
    COMMAND "${CXX_TOOL}" -shared -fPIC
        "-Wl,-soname,${BUNDLE_SONAME}"
        -o "${BUNDLE_SO}"
        "-Wl,--whole-archive" "${SRT_LIBA}" "-Wl,--no-whole-archive"
        "${MBEDTLS_LIBA}" "${MBEDX509_LIBA}" "${MBEDCRYPTO_LIBA}"
        "-Wl,--exclude-libs,${_exclude_arg}"
        -pthread
    RESULT_VARIABLE _link_rc
    ERROR_VARIABLE  _link_err)
if(NOT _link_rc EQUAL 0)
    message(FATAL_ERROR "linking ${BUNDLE_SONAME} failed:\n${_link_err}")
endif()

message(STATUS "promeki-srt-bundle: wrote ${BUNDLE_SO} (mbedTLS-3.6 symbols localized)")

# Split debug symbols out of the freshly-linked bundle (no-op when OBJCOPY
# is unset).  Runs only as part of this OUTPUT command, which re-links from
# scratch whenever its inputs change, so the captured .debug stays complete.
include(${CMAKE_CURRENT_LIST_DIR}/PromekiStripSo.cmake)
promeki_strip_so("${BUNDLE_SO}" "${DEBUGDIR}" "${OBJCOPY}" "--strip-unneeded")
