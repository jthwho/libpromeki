add_executable(promeki-pcap main.cpp)
target_link_libraries(promeki-pcap PRIVATE promeki::promeki)
install(TARGETS promeki-pcap RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
