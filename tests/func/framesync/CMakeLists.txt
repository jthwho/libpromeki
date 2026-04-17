add_executable(framesync-functest
    main.cpp
)
target_link_libraries(framesync-functest PRIVATE
    promeki::promeki
)
