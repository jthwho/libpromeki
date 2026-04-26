# HTTP Explorer Demo
#
# Bundles the demo's static UI assets (index.html, app.js, style.css)
# into a cirf resource archive linked into the demo binary.  The
# resources land at the virtual path ":/demo/explorer/" thanks to the
# folder structure declared in resources.json plus the empty mount
# prefix used by PROMEKI_REGISTER_RESOURCES in builtinresources.cpp.
#
# cirf_add_resources() is brought into scope by the main project's
# CMakeLists.txt (it includes CIRFAddResources.cmake).  When this
# demo is built standalone, find_package(promeki) does not bring
# that helper in — so we guard the cirf path and fall back to a
# plain executable that won't have the static UI bundled.

add_executable(http-explorer-demo main.cpp)
target_link_libraries(http-explorer-demo PRIVATE promeki::promeki)

if(COMMAND cirf_add_resources)
    cirf_add_resources(http_explorer_demo_resources
        CONFIG     ${CMAKE_CURRENT_SOURCE_DIR}/resources.json
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/http_explorer_demo_resources)
    target_link_libraries(http-explorer-demo
        PRIVATE http_explorer_demo_resources)
    target_include_directories(http-explorer-demo
        PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/http_explorer_demo_resources)
    target_sources(http-explorer-demo PRIVATE builtinresources.cpp)
endif()

install(TARGETS http-explorer-demo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
