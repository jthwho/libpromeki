add_executable(mediaplay
    main.cpp
    cli.cpp
    pipeline.cpp
    stage.cpp
)
target_link_libraries(mediaplay PRIVATE promeki::promeki promeki::sdl)
target_compile_definitions(mediaplay PRIVATE
    PROMEKI_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
install(TARGETS mediaplay RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
