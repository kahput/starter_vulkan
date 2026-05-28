file(GLOB_RECURSE SOURCES "src/*.c" "${CMAKE_SOURCE_DIR}/engine/src/core/*.c" "${CMAKE_SOURCE_DIR}/engine/src/platform/filesystem.c")
add_executable(tools ${SOURCES})

add_compile_options(json_parser PRIVATE -fsanitize=address)
add_link_options(tools PRIVATE -fsanitize=address)

target_link_libraries(tools m)
target_include_directories(tools
                           PUBLIC "${CMAKE_SOURCE_DIR}/engine/src")

target_compile_options(
tools
PRIVATE -fvisibility=hidden
        -Wall
        -O0
        -pedantic
        -Wextra
        -Werror
        -Wno-unused-parameter
        -Wno-unused-variable)

