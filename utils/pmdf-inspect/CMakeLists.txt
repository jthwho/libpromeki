add_executable(pmdf-inspect
    main.cpp
)
target_link_libraries(pmdf-inspect PRIVATE promeki::promeki)
install(TARGETS pmdf-inspect RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
