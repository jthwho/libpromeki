add_executable(mediaplay
    main.cpp
    cli.cpp
    helpformat.cpp
    sdlsupport.cpp
    stage.cpp
)
target_link_libraries(mediaplay PRIVATE promeki::promeki)
target_compile_definitions(mediaplay PRIVATE
    PROMEKI_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# The SDL display sink is optional: link promeki::sdl and flip
# MEDIAPLAY_HAVE_SDL on when the library was built with SDL3 support;
# otherwise the SDL-specific code paths in mediaplay are compiled out
# and the user must supply an explicit -d destination.
if(PROMEKI_BUILD_SDL)
    target_link_libraries(mediaplay PRIVATE promeki::sdl)
    target_compile_definitions(mediaplay PRIVATE MEDIAPLAY_HAVE_SDL=1)
else()
    target_compile_definitions(mediaplay PRIVATE MEDIAPLAY_HAVE_SDL=0)
endif()

install(TARGETS mediaplay RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
