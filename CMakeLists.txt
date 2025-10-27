cmake_minimum_required(VERSION 3.20)
project(SokobanOpenGL LANGUAGES CXX)

# Use C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# If user sets VCPKG toolchain externally, we won't override. Otherwise, allow default.
# Recommend invoking with: -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake

# Dependencies via vcpkg: glfw3, glad, glm, assimp, stb (header only)
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
# stb is header-only; many vcpkg ports expose it as 'stb::stb', but we can include header directly.

add_executable(SokobanOpenGL
    src/main.cpp
    src/shader.h
    src/camera.h
    src/mesh.h
    src/model.h
)

target_include_directories(SokobanOpenGL PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(SokobanOpenGL PRIVATE glfw glad::glad glm::glm assimp::assimp)

# Copy runtime assets next to the binary
add_custom_command(TARGET SokobanOpenGL POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:SokobanOpenGL>/shaders
)
add_custom_command(TARGET SokobanOpenGL POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:SokobanOpenGL>/assets
)