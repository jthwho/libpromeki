# SRT Demo
#
# Self-contained loopback demo of the SRT API:
#   - SrtServer listens on 127.0.0.1:<port>
#   - A worker thread runs an SrtSocket caller that connects with
#     a shared passphrase and sends a few payloads.
#   - The server side accepts via SrtEpoll's push-dispatch, reads
#     each message, and prints it.
#
# Built only when SRT is enabled.  Skipped silently otherwise so
# building demos remains tolerant of feature subsets.
if(PROMEKI_ENABLE_SRT)
    add_executable(srt-demo main.cpp)
    target_link_libraries(srt-demo PRIVATE promeki::promeki)
    install(TARGETS srt-demo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
