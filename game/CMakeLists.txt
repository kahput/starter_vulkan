file(GLOB_RECURSE SOURCES "src/*.c" "src/*.h")
add_library(game SHARED ${SOURCES})

# add_custom_command(TARGET Game POST_BUILD COMMAND ${CMAKE_COMMAND} -E
# copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/res/
# $<TARGET_FILE_DIR:Minecraft>/res COMMENT "Copying res to
# $<TARGET_FILE_DIR:Game>")

target_include_directories(game PRIVATE "${CMAKE_SOURCE_DIR}/engine/src")
target_compile_options(game PRIVATE -Wall -pedantic -Wextra -Werror
                                    -Wno-unused-parameter -Wno-unused-variable)
