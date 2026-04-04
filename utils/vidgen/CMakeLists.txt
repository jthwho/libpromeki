add_executable(vidgen main.cpp)
target_link_libraries(vidgen PRIVATE promeki::promeki)
target_compile_definitions(vidgen PRIVATE
    PROMEKI_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
if(PROMEKI_BUILD_SDL)
    target_link_libraries(vidgen PRIVATE promeki::sdl)
    target_compile_definitions(vidgen PRIVATE PROMEKI_HAS_SDL=1)
endif()
install(TARGETS vidgen RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
