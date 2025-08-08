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

include(FetchContent)
FetchContent_Declare(
  cglm
  GIT_REPOSITORY https://github.com/recp/cglm.git
  FIND_PACKAGE_ARGS NAMES cglm
)

find_package(Vulkan REQUIRED)

file(GLOB_RECURSE SOURCES "src/*.c" "src/*.h" )
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC "./src/")

# Modern CMake approach with target_* commands
target_include_directories(${PROJECT_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS})

# Platform-specific configurations
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_WIN32_KHR)
else()
    # Linux-specific configuration
    target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_XCB_KHR)
endif()

if(MSVC)
    # /WX
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
    target_link_libraries(${PROJECT_NAME} Vulkan::Vulkan cglm)
else()
    # -Wpedantic
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable)
    target_link_libraries( ${PROJECT_NAME} xcb Vulkan::Vulkan cglm m)
endif()
