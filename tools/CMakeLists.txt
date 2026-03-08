file(GLOB_RECURSE SOURCES "src/*.c" "${CMAKE_SOURCE_DIR}/engine/src/core/*.c" "${CMAKE_SOURCE_DIR}/engine/src/platform/filesystem.c")
add_executable(json_parser ${SOURCES})

add_compile_options(json_parser PRIVATE -fsanitize=address)
add_link_options(json_parser PRIVATE -fsanitize=address)

target_link_libraries(json_parser m)
target_include_directories(json_parser
                           PUBLIC "${CMAKE_SOURCE_DIR}/engine/src")

target_compile_options(
json_parser
PRIVATE -fvisibility=hidden
        -Wall
        -O0
        -pedantic
        -Wextra
        -Werror
        -Wno-unused-parameter
        -Wno-unused-variable)

