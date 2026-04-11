add_executable(crashtest main.cpp)
target_link_libraries(crashtest PRIVATE promeki::promeki)
install(TARGETS crashtest RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
