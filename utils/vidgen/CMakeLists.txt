add_executable(vidgen main.cpp)
target_link_libraries(vidgen PRIVATE promeki::proav promeki::network)
target_compile_definitions(vidgen PRIVATE
    PROMEKI_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
install(TARGETS vidgen RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
