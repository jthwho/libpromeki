add_executable(promeki-bench
    main.cpp
    benchparams.cpp
    cases/csc.cpp
)
target_link_libraries(promeki-bench PRIVATE promeki::promeki)
