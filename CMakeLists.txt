cmake_minimum_required(VERSION 3.10)
project(drill CXX)

set(CMAKE_CXX_STANDARD 17)

add_definitions(-DBX_CONFIG_DEBUG)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(assimp REQUIRED)


include_directories(
    ${CMAKE_SOURCE_DIR}/bx/include
    ${CMAKE_SOURCE_DIR}/bimg/include
    ${CMAKE_SOURCE_DIR}/bgfx/include
    /usr/include/stb
)

add_executable(drill main.cpp)

target_link_libraries(drill
    ${CMAKE_SOURCE_DIR}/bgfx/.build/linux64_gcc/bin/libbgfx-shared-libDebug.so
    assimp
    glfw
    ${OPENGL_gl_LIBRARY}
)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/textures DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(INSTALL
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
    TYPE FILE
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/brdf_lut.ktx
        ${CMAKE_CURRENT_SOURCE_DIR}/Drill_01_1k.gltf
        ${CMAKE_CURRENT_SOURCE_DIR}/Drill_01.bin
        ${CMAKE_CURRENT_SOURCE_DIR}/irradiance.ktx
        ${CMAKE_CURRENT_SOURCE_DIR}/skybox.ktx
        ${CMAKE_CURRENT_SOURCE_DIR}/vs_skybox.bin
        ${CMAKE_CURRENT_SOURCE_DIR}/fs_skybox.bin
        ${CMAKE_CURRENT_SOURCE_DIR}/vs_drill.bin
        ${CMAKE_CURRENT_SOURCE_DIR}/fs_drill.bin
)
