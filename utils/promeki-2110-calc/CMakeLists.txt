add_executable(promeki-2110-calc main.cpp)
target_link_libraries(promeki-2110-calc PRIVATE promeki::promeki)
install(TARGETS promeki-2110-calc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
