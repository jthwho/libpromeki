add_executable(roundtrip-functest
    main.cpp
)
target_link_libraries(roundtrip-functest PRIVATE
    promeki::promeki
)
