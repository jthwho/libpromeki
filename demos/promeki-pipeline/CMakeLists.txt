if(TARGET promeki::promeki)
    cirf_add_resources(promeki_pipeline_resources
        CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/resources.json
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/promeki_pipeline_resources)

    # The pipeline routing / management code is split into a static lib
    # so the test target can link it directly without spinning up the
    # full demo binary.
    add_library(promeki-pipeline-core STATIC
        pipelinemanager.cpp
        apiroutes.cpp
        eventbroadcaster.cpp
        pipelinesettings.cpp)
    set_target_properties(promeki-pipeline-core PROPERTIES
        POSITION_INDEPENDENT_CODE ON)
    target_include_directories(promeki-pipeline-core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(promeki-pipeline-core PUBLIC promeki::promeki)

    add_executable(promeki-pipeline main.cpp)
    target_link_libraries(promeki-pipeline PRIVATE
        promeki-pipeline-core
        promeki_pipeline_resources)
    target_include_directories(promeki-pipeline PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/promeki_pipeline_resources)

    install(TARGETS promeki-pipeline RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
