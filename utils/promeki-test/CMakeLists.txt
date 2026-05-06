add_executable(promeki-test
    main.cpp
    testcontext.cpp
    testrunner.cpp
    cases/pipeline_common.cpp
    cases/audio.cpp
    cases/codec.cpp
    cases/framebridge.cpp
    cases/ndi.cpp
    cases/roundtrip.cpp
    cases/rtp.cpp
)

target_include_directories(promeki-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(promeki-test PRIVATE
    promeki::promeki
)
