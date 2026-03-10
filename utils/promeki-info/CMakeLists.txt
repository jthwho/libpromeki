add_executable(promeki-info main.cpp)
target_link_libraries(promeki-info PRIVATE promeki::core)
install(TARGETS promeki-info RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
