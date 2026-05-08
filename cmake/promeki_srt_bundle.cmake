# promeki_srt_bundle.cmake
#
# Helper script invoked at build time (via cmake -P) to combine libsrt.a
# with the three isolated mbedTLS-3.6 static archives into a single
# static library where every mbedTLS-3.6 symbol is marked LOCAL — so
# the symbols cannot collide with the *main* mbedTLS-4.x stack that
# the rest of libpromeki.so links against.
#
# Required vars (set by the parent CMakeLists.txt via -D...):
#   BUNDLE_OBJ        path of the partial-linked object to produce
#   BUNDLE_LIB        path of the final static archive
#   LOCALSYMS_FILE    path of the generated localize-symbols list
#   SRT_LIBA          input libsrt.a (built by srt_ep)
#   MBEDTLS_LIBA      input libmbedtls.a    (3.6, built by srt_mbedtls_ep)
#   MBEDX509_LIBA     input libmbedx509.a   (3.6)
#   MBEDCRYPTO_LIBA   input libmbedcrypto.a (3.6)
#   LD_TOOL           path to ld
#   OBJCOPY_TOOL      path to objcopy
#   AR_TOOL           path to ar
#   NM_TOOL           path to nm

foreach(_var BUNDLE_OBJ BUNDLE_LIB LOCALSYMS_FILE
             SRT_LIBA MBEDTLS_LIBA MBEDX509_LIBA MBEDCRYPTO_LIBA
             LD_TOOL OBJCOPY_TOOL AR_TOOL NM_TOOL)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "promeki_srt_bundle.cmake: ${_var} is not set")
    endif()
endforeach()

# Make sure the output directory exists.
get_filename_component(_outdir "${BUNDLE_LIB}" DIRECTORY)
file(MAKE_DIRECTORY "${_outdir}")

# 1. Enumerate every GLOBAL symbol defined by the isolated mbedTLS-3.6
#    archives — these are the symbols we want to localize.  `nm -g
#    --defined-only` prints addr/type/name; the symbol name is the
#    last column.  We strip leading whitespace and de-duplicate.
set(_mbedtls_archives
    "${MBEDTLS_LIBA}"
    "${MBEDX509_LIBA}"
    "${MBEDCRYPTO_LIBA}"
)

set(_all_syms "")
foreach(_a ${_mbedtls_archives})
    execute_process(
        COMMAND "${NM_TOOL}" -g --defined-only --format=posix "${_a}"
        OUTPUT_VARIABLE _nm_out
        RESULT_VARIABLE _nm_rc
        ERROR_VARIABLE  _nm_err)
    if(NOT _nm_rc EQUAL 0)
        message(FATAL_ERROR "nm failed on ${_a}: ${_nm_err}")
    endif()
    # Each line: "<symbol> <type> [<addr> [<size>]]"  — POSIX format.
    # Skip blank lines and archive-member headers ("name.o:").
    string(REPLACE "\n" ";" _nm_lines "${_nm_out}")
    foreach(_line ${_nm_lines})
        string(STRIP "${_line}" _line)
        if(_line STREQUAL "" OR _line MATCHES ":$")
            continue()
        endif()
        # Symbol is the first whitespace-separated token.
        string(REGEX REPLACE "[ \t].*$" "" _sym "${_line}")
        if(_sym STREQUAL "")
            continue()
        endif()
        list(APPEND _all_syms "${_sym}")
    endforeach()
endforeach()

list(REMOVE_DUPLICATES _all_syms)
list(SORT _all_syms)
list(LENGTH _all_syms _n_syms)
message(STATUS "promeki-srt-bundle: localizing ${_n_syms} mbedTLS-3.6 symbols")

# Write the symbol list, one per line, for objcopy --localize-symbols.
set(_localsyms_content "")
foreach(_s ${_all_syms})
    string(APPEND _localsyms_content "${_s}\n")
endforeach()
file(WRITE "${LOCALSYMS_FILE}" "${_localsyms_content}")

# 2. Partial-link libsrt.a + the three mbedTLS-3.6 archives into one
#    object.  --whole-archive on libsrt forces in every libsrt member
#    (we want all of SRT to be present).  The mbedTLS archives are
#    pulled in selectively by reference; haicrypt symbols inside libsrt
#    drag in only the mbedTLS .o's they actually use.
execute_process(
    COMMAND "${LD_TOOL}" -r
        --whole-archive    "${SRT_LIBA}"
        --no-whole-archive "${MBEDTLS_LIBA}" "${MBEDX509_LIBA}" "${MBEDCRYPTO_LIBA}"
        -o "${BUNDLE_OBJ}"
    RESULT_VARIABLE _ld_rc
    ERROR_VARIABLE  _ld_err)
if(NOT _ld_rc EQUAL 0)
    message(FATAL_ERROR "ld -r failed bundling SRT + mbedTLS-3.6:\n${_ld_err}")
endif()

# 3. Localize all collected mbedTLS-3.6 symbols inside the partial-linked
#    object.  After this pass, the symbols still resolve internally
#    (SRT haicrypt → mbedTLS-3.6) but become invisible to anything
#    linking against the bundle.
execute_process(
    COMMAND "${OBJCOPY_TOOL}" "--localize-symbols=${LOCALSYMS_FILE}" "${BUNDLE_OBJ}"
    RESULT_VARIABLE _oc_rc
    ERROR_VARIABLE  _oc_err)
if(NOT _oc_rc EQUAL 0)
    message(FATAL_ERROR "objcopy --localize-symbols failed:\n${_oc_err}")
endif()

# 4. Wrap the localized object into a static archive.  Delete any
#    stale archive first — `ar rcs` appends to existing archives,
#    which would double the bundle on rebuilds.
file(REMOVE "${BUNDLE_LIB}")
execute_process(
    COMMAND "${AR_TOOL}" rcs "${BUNDLE_LIB}" "${BUNDLE_OBJ}"
    RESULT_VARIABLE _ar_rc
    ERROR_VARIABLE  _ar_err)
if(NOT _ar_rc EQUAL 0)
    message(FATAL_ERROR "ar rcs failed:\n${_ar_err}")
endif()

message(STATUS "promeki-srt-bundle: wrote ${BUNDLE_LIB}")
