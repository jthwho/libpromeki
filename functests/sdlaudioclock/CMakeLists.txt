add_executable(sdlaudioclock-functest
    main.cpp
)
target_link_libraries(sdlaudioclock-functest PRIVATE
    promeki::promeki
    promeki::sdl
)
