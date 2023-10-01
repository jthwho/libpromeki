
set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(json)

ExternalProject_Add(
    libjpeg-turbo
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libjpeg-turbo
    STEP_TARGETS build
    EXCLUDE_FROM_ALL TRUE
)

