add_executable(anc-framesync-demo main.cpp)
target_link_libraries(anc-framesync-demo PRIVATE promeki::promeki)
install(TARGETS anc-framesync-demo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
