file(GLOB_RECURSE SOURCES "src/*.c" "src/*.h" "deps/*/*.c" "deps/*/*.h")
add_executable(${PROJECT_NAME} ${SOURCES})

# TEMP
include(FetchContent)
FetchContent_Declare(
  cglm
  GIT_REPOSITORY https://github.com/recp/cglm.git
  FIND_PACKAGE_ARGS NAMES cglm)

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
  target_link_libraries(${PROJECT_NAME} Vulkan::Vulkan cglm)
elseif(UNIX AND NOT APPLE)
  find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner)
  if(NOT WAYLAND_SCANNER_EXECUTABLE)
    message(FATAL_ERROR "Failed to find wayland-scanner")
  endif()

  set(protocol_directory "${CMAKE_SOURCE_DIR}/engine/deps/wayland")
  macro(generate_wayland_protocol protocol_file)
    set(protocol_file_path "${protocol_directory}/${protocol_file}")

    string(REGEX REPLACE "\\.xml$" "-client-protocol.h" header_file
                         ${protocol_file})
    string(REGEX REPLACE "\\.xml$" "-client-protocol-code.h" code_file
                         ${protocol_file})

    add_custom_command(
      OUTPUT ${header_file}
      COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header
              "${protocol_file_path}" ${header_file}
      DEPENDS "${protocol_file_path}"
      VERBATIM)

    add_custom_command(
      OUTPUT ${code_file}
      COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code
              "${protocol_file_path}" ${code_file}
      DEPENDS "${protocol_file_path}"
      VERBATIM)

    target_sources(${PROJECT_NAME} PRIVATE ${header_file} ${code_file})
  endmacro()

  generate_wayland_protocol("wayland.xml")
  generate_wayland_protocol("viewporter.xml")
  generate_wayland_protocol("xdg-shell.xml")
  generate_wayland_protocol("fractional-scale-v1.xml")
  generate_wayland_protocol("pointer-constraints-unstable-v1.xml")
  generate_wayland_protocol("relative-pointer-unstable-v1.xml")
  generate_wayland_protocol("tablet-v2.xml")
  generate_wayland_protocol("cursor-shape-v1.xml")

  target_compile_definitions(${PROJECT_NAME} PRIVATE PLATFORM_WAYLAND
                                                     PLATFORM_X11)
  target_compile_options(
    ${PROJECT_NAME}
    PRIVATE -fvisibility=hidden
            -Wall
            -pedantic
            -Wextra
            -Werror
            -Wno-unused-parameter
            -Wno-unused-variable)

  target_link_options(${PROJECT_NAME} PRIVATE -Wl,--export-dynamic)
  target_link_libraries(${PROJECT_NAME} dl Vulkan::Vulkan cglm m)

else()
  message(FATAL_ERROR "Windows and Linux only supported platforms")
endif()
