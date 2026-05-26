add_executable(promeki-fetch-model main.cpp)
target_link_libraries(promeki-fetch-model PRIVATE promeki::promeki)
install(TARGETS promeki-fetch-model RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
