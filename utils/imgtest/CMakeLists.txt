add_executable(imgtest main.cpp)
target_link_libraries(imgtest PRIVATE promeki::promeki)
target_compile_definitions(imgtest PRIVATE
    PROMEKI_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
