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
# In docs-only mode the doxygen-awesome-css submodule may not be checked
# out; we include its stylesheets only when present and otherwise fall
# back to Doxygen's default theme.

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

    set(PROMEKI_DOCS_VERSION "${BUILD_INFO_VERSION}" CACHE STRING
        "Version string shown in generated documentation")

    # Optional doxygen-awesome-css submodule.  When the submodule is checked
    # out we use its modern theme; otherwise we leave HTML_EXTRA_STYLESHEET
    # empty and fall back to Doxygen's built-in styles.  Keeping this
    # optional lets the docs build succeed without `git submodule update`.
    set(_awesome_dir "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/doxygen-awesome-css")
    if(EXISTS "${_awesome_dir}/doxygen-awesome.css")
        set(PROMEKI_DOCS_EXTRA_STYLESHEET
            "${_awesome_dir}/doxygen-awesome.css ${_awesome_dir}/doxygen-awesome-sidebar-only.css")
    else()
        set(PROMEKI_DOCS_EXTRA_STYLESHEET "")
        message(STATUS "doxygen-awesome-css submodule not checked out; using default Doxygen theme")
    endif()

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
