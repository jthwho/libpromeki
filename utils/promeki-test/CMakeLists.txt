add_executable(promeki-test
    main.cpp
    testcontext.cpp
    testrunner.cpp
    cases/pipeline_common.cpp
    cases/audio.cpp
    cases/captions.cpp
    cases/codec.cpp
    cases/framebridge.cpp
    cases/ndi.cpp
    cases/roundtrip.cpp
    cases/rtp.cpp
    cases/rtp_ffmpeg.cpp
    cases/rtpchaos.cpp
    cases/rtpchaosshim.cpp
    cases/videocarrier.cpp
)

target_include_directories(promeki-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Captions case loads etc/substest.srt from the source tree; passing
# the source dir through as a compile definition keeps the fixture
# path resolution independent of CWD.  The captions case file is the
# only consumer today; other cases ignore the macro.
target_compile_definitions(promeki-test PRIVATE
    PROMEKI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../.."
)

target_link_libraries(promeki-test PRIVATE
    promeki::promeki
)
