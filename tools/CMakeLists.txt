file(GLOB_RECURSE SOURCES "src/*.c" "${CMAKE_SOURCE_DIR}/engine/src/core/*.c" "${CMAKE_SOURCE_DIR}/engine/src/platform/filesystem.c")

set(GENERATOR generator)

add_executable(${GENERATOR} ${SOURCES})

add_compile_options(${GENERATOR} PRIVATE -fsanitize=address)
add_link_options(${GENERATOR} PRIVATE -fsanitize=address)

target_link_libraries(${GENERATOR} m)
target_include_directories(${GENERATOR} PUBLIC "${CMAKE_SOURCE_DIR}/engine/src")

target_compile_options(
${GENERATOR}
PRIVATE -fvisibility=hidden
        -Wall
        -std=gnu11
        -O0
        -pedantic
        -Wextra
        -Werror
        -Wno-unused-parameter
        -Wno-unused-variable)
