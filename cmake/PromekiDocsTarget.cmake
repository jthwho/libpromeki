# promeki_setup_docs_target()
#
# Configures the `docs` (Doxygen) target.  Used in two modes:
#
#   1. Normal build — invoked from the main CMakeLists.txt after the rest of
#      the project is configured.  Picks up BUILD_INFO_VERSION /
#      BUILD_INFO_REPOIDENT from the surrounding scope.
#   2. Docs-only build (PROMEKI_DOCS_ONLY=ON) — invoked early, before any
#      libraries / third-party deps are configured.  Derives the version
#      label from the VERSION file (already parsed into APP_VERSION at
#      project() time) and the commit hash from `git rev-parse HEAD`, so
#      no submodules are required for the docs build.
#
# Styling: HTML_EXTRA_STYLESHEET points at docs/promeki_doxygen.css, a
# minimal override that lifts the version (PROJECT_NUMBER) out of Doxygen's
# default 50%-size rendering.  Defaults to Doxygen's built-in theme
# otherwise.

function(promeki_setup_docs_target)
    set(PROMEKI_DOCS_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/doxygen" CACHE PATH
        "Output directory for generated documentation")

    find_package(Doxygen QUIET)
    if(NOT DOXYGEN_FOUND)
        if(PROMEKI_BUILD_DOCS OR PROMEKI_DOCS_ONLY)
            message(FATAL_ERROR
                "PROMEKI_BUILD_DOCS / PROMEKI_DOCS_ONLY is ON but Doxygen was not found")
        endif()
        return()
    endif()

    # Version label and commit hash for the footer.  In docs-only mode
    # BUILD_INFO_* are not yet defined, so derive minimal substitutes.
    if(NOT DEFINED BUILD_INFO_VERSION)
        set(BUILD_INFO_VERSION "${APP_VERSION}")
    endif()
    # Pretty version for display in the docs (PROJECT_NUMBER and footer):
    # MAJOR.MINOR.PATCH[-stage[N]][+buildN], matching the suffix recipe used
    # for BUILD_INFO_IDENT.  Composed here from APP_VERSION_* (always set at
    # project() time) so docs-only mode gets the same string without needing
    # the rest of the build to configure first.
    if(APP_VERSION_STAGE_NAME STREQUAL "release")
        set(BUILD_INFO_VERSION_FULL "${BUILD_INFO_VERSION}")
    elseif(APP_VERSION_STAGE_NAME STREQUAL "rc")
        set(BUILD_INFO_VERSION_FULL "${BUILD_INFO_VERSION}-rc${APP_VERSION_STAGE_NUM}")
    elseif(APP_VERSION_STAGE_NUM EQUAL 1)
        set(BUILD_INFO_VERSION_FULL "${BUILD_INFO_VERSION}-${APP_VERSION_STAGE_NAME}")
    else()
        set(BUILD_INFO_VERSION_FULL "${BUILD_INFO_VERSION}-${APP_VERSION_STAGE_NAME}${APP_VERSION_STAGE_NUM}")
    endif()
    if(APP_VERSION_BUILD AND NOT APP_VERSION_BUILD STREQUAL "0")
        set(BUILD_INFO_VERSION_FULL "${BUILD_INFO_VERSION_FULL}+${APP_VERSION_BUILD}")
    endif()
    if(NOT DEFINED BUILD_INFO_REPOIDENT)
        find_package(Git QUIET)
        if(GIT_FOUND)
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
                WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
                OUTPUT_VARIABLE BUILD_INFO_REPOIDENT
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()
        if(NOT BUILD_INFO_REPOIDENT)
            set(BUILD_INFO_REPOIDENT "unknown")
        endif()
    endif()
    if(NOT DEFINED BUILD_INFO_DATE)
        string(TIMESTAMP BUILD_INFO_DATE "%Y-%m-%d")
    endif()
    if(NOT DEFINED BUILD_INFO_TIME)
        string(TIMESTAMP BUILD_INFO_TIME "%H:%M:%S")
    endif()

    string(SUBSTRING "${BUILD_INFO_REPOIDENT}" 0 7 BUILD_INFO_REPOIDENT_SHORT)

    # Plain (non-cache) variable so a VERSION/BUILD edit immediately flows
    # into the next docs build instead of being shadowed by a stale cache entry.
    set(PROMEKI_DOCS_VERSION "${BUILD_INFO_VERSION_FULL}")

    # Local stylesheet that makes the project version prominent in the
    # generated header; everything else defers to Doxygen's defaults.
    set(PROMEKI_DOCS_EXTRA_STYLESHEET
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/promeki_doxygen.css")

    set(DOXYGEN_IN  ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/docs/footer.html.in
        ${CMAKE_CURRENT_BINARY_DIR}/doxygen_footer.html @ONLY)
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

    file(GLOB PROMEKI_DOCS_IMAGES ${CMAKE_CURRENT_SOURCE_DIR}/docs/*.jpg)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PROMEKI_DOCS_OUTPUT_DIR}/html/docs
        COMMAND ${CMAKE_COMMAND} -E copy ${PROMEKI_DOCS_IMAGES} ${PROMEKI_DOCS_OUTPUT_DIR}/html/docs/
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM)
    if(PROMEKI_BUILD_DOCS OR PROMEKI_DOCS_ONLY)
        add_custom_target(docs-all ALL DEPENDS docs)
    endif()
endfunction()
