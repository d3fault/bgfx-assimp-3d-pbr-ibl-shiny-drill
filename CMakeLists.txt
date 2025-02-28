cmake_minimum_required(VERSION 3.10)
project(DuckExample CXX)

set(CMAKE_CXX_STANDARD 17)

# As requested
add_definitions(-DBX_CONFIG_DEBUG)

# You may need to find OpenGL (depending on your platform)
find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)

# Make sure you have installed and can find GLFW + Assimp
# If needed, set GLFW3_DIR or Assimp_DIR or use pkg-config, etc.
find_package(glfw3 REQUIRED)
find_package(assimp REQUIRED)

# For stb_image, we just include the header; no separate lib needed.
# We add an include path for it if you store it somewhere else.

# Include paths:
include_directories(
    ${CMAKE_SOURCE_DIR}/bx/include
    ${CMAKE_SOURCE_DIR}/bimg/include
    ${CMAKE_SOURCE_DIR}/bgfx/include
    # If you keep stb_image.h in a custom folder, add that path here:
    /usr/include/stb
)

# Source
add_executable(duck
    main.cpp
)

# Link in the debug bc libs as requested (for WASM), or adapt for static libs on other platforms.
target_link_libraries(duck
    ${CMAKE_SOURCE_DIR}/bgfx/.build/linux64_gcc/bin/libbgfx-shared-libDebug.so
    assimp
    glfw
    ${OPENGL_gl_LIBRARY}
)

# If you need extra libraries on your platform (X11, pthread, etc.), add them similarly.
