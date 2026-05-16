# ANC Frame-Sync Demo
#
# Command-line driver showing AncFrameSync with CmdLineParser /
# String::format / Application utilities.
#
# Built only when ProAV is enabled.  Skipped silently otherwise so
# building demos remains tolerant of feature subsets.
if(PROMEKI_ENABLE_PROAV)
    add_executable(anc-framesync-demo main.cpp)
    target_link_libraries(anc-framesync-demo PRIVATE promeki::promeki)
    install(TARGETS anc-framesync-demo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
