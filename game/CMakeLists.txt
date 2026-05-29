set(META_OUT_H "${CMAKE_SOURCE_DIR}/game/src/meta_generated.h")
set(META_OUT_C "${CMAKE_SOURCE_DIR}/game/src/meta_generated.c")

file(GLOB_RECURSE INTROSPECTION_HEADERS
    "${CMAKE_SOURCE_DIR}/engine/src/*.h"
    "${CMAKE_SOURCE_DIR}/game/src/*.h"
)

# add_custom_command(
#     OUTPUT ${META_OUT_H} ${META_OUT_C}
#     COMMAND generator ${META_OUT_H} ${META_OUT_C} ${INTROSPECTION_HEADERS}
#     DEPENDS generator ${INTROSPECTION_HEADERS}
#     COMMENT "Running Introspection Generator..."
#     VERBATIM
# )

file(GLOB_RECURSE SOURCES "src/*.c" "src/*.h")
add_library(game SHARED ${SOURCES} ${META_OUT_C} ${META_OUT_H})

# add_custom_command(TARGET Game POST_BUILD COMMAND ${CMAKE_COMMAND} -E
# copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/res/
# $<TARGET_FILE_DIR:Minecraft>/res COMMENT "Copying res to
# $<TARGET_FILE_DIR:Game>")

target_include_directories(game PRIVATE "src" "${CMAKE_SOURCE_DIR}/engine/src")
target_compile_options(game PRIVATE -Wall -pedantic -Wextra -Werror
    -Wno-unused-parameter -Wno-unused-variable -Wno-override-init)
