file(GLOB_RECURSE SOURCES "src/*.c" "src/*.h" "deps/*/*.c" "deps/*/*.h")
add_executable(${PROJECT_NAME} ${SOURCES})

find_package(Vulkan REQUIRED)

add_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=address)
add_link_options(${PROJECT_NAME} PRIVATE -fsanitize=address)

target_include_directories(${PROJECT_NAME}
                           PUBLIC "${CMAKE_SOURCE_DIR}/engine/src")
target_include_directories(
  ${PROJECT_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS} "${CMAKE_BINARY_DIR}/engine"
                          "${CMAKE_SOURCE_DIR}/engine/deps")

# Platform-specific configurations
if(WIN32)
  target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_WIN32_KHR)
  target_compile_options(${PROJECT_NAME} PRIVATE /W4)
  target_link_libraries(${PROJECT_NAME} Vulkan::Vulkan)
elseif(UNIX)
  target_compile_options(
    ${PROJECT_NAME}
    PRIVATE -fvisibility=hidden
            -Wall
            -O0
            -pedantic
            -Wextra
            -Werror
            -Wno-unused-parameter
            -Wno-unused-variable)

  target_link_options(${PROJECT_NAME} PRIVATE -Wl,--export-dynamic)
  target_link_libraries(${PROJECT_NAME} dl xcb Vulkan::Vulkan m)
else()
  message(FATAL_ERROR "Windows and Linux only supported platforms")
endif()
