add_executable(testrender main.cpp)
target_link_libraries(testrender PRIVATE promeki::promeki)
target_compile_definitions(testrender PRIVATE
    PROMEKI_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
