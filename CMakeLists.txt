cmake_minimum_required(VERSION 3.11)
project(game)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    set(CONFIG $<CONFIG>)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
else()
    set(CONFIG ${CMAKE_BUILD_TYPE})
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${CONFIG})
endif()

file(GLOB_RECURSE SOURCES "src/*.c" "src/*.h" )
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC "./src/")


if(MSVC)
    # /WX
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    # -Wpedantic
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable)
    target_link_libraries( ${PROJECT_NAME} xcb)
endif()
