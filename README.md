# bgfx assimp 3d pbr ibl shiny drill

Load a 3d asset using assimp and render it using physical based rendering and image-based lighting against a skybox. I have no clue if I implemented PBR/IBL correctly I just copy/pasted what ChatGPT said to do. Uses bgfx and glfw3, compiling for WebAssembly (wasm) using Emscripten. Additionally compiles to native Linux/X11. Tested on Debian Bookworm (12), might work on Debian Bullseye (11). Very specific versions of bgfx and Emscripten are targeted; upgrading to newer versions may or may not work.

[Demo](http://d3fault.github.io/wasm-3d-demos/bgfx-assimp-3d-pbr-ibl-shiny-drill/index.html) --- [All Demos](http://d3fault.github.io/wasm-3d-demos/index.html)

## WebAssembly

### Dependencies (WebAssembly)

* `# apt install git python3 xz-utils cmake make`

#### Install emscripten sdk and prepare build environment (WebAssembly)

* `git clone https://github.com/emscripten-core/emsdk.git`
* `cd emsdk`
* `./emsdk install 3.1.51 #only tested version. I've heard newer builds don't work, but they might now`
* `./emsdk activate 3.1.51`
* `source ./emsdk_env.sh`
* `cd ..`

### Building (WebAssembly)

* `git clone https://github.com/d3fault/bgfx-assimp-3d-pbr-ibl-shiny-drill.git`
* `cd bgfx-assimp-3d-pbr-ibl-shiny-drill`
* git clone and compile bgfx into ./bgfx i'm too lazy to put this in the readme right now. compile shaderc too for the host. see the readme in this repo for more instructions kinda: https://github.com/d3fault/fly-around-bullet-physics-bgfx-wasm-template-starting-point
* git clone and compile assimp and edit our CMakeLists.txt which has hardcoded paths to assimp
* `emcmake cmake -B buildwasm`
* `cd buildwasm`
* `emmake make`

### Running (WebAssembly)

* `python -m SimpleHTTPServer 8080 #or other webserver`
* In your web browser: http://localhost:8080/index.html

## Native Linux/X11

### Dependencies (Linux/X11)

`# apt install git build-essential cmake libglfw3-dev libboost-all-dev libassimp-dev`

### Building (Linux/X11)

* `git clone https://github.com/d3fault/bgfx-assimp-3d-pbr-ibl-shiny-drill.git`
* `cd bgfx-assimp-3d-pbr-ibl-shiny-drill`
* git clone and compile bgfx into ./bgfx i'm too lazy to put this in the readme right now. compile shaderc too for the host. see the readme in this repo for more instructions kinda: https://github.com/d3fault/fly-around-bullet-physics-bgfx-wasm-template-starting-point
* `cmake -B buildnative`
* `cd buildnative`
* `cmake --build . --parallel`

### Running (Linux/X11)

* `./bgfx-assimp-3d-pbr-ibl-shiny-drill`

## TODO (patches welcome)

* Release builds
* Multi-threading(?)
* Newer Emscripten and bgfx versions
* Auto-detect screen dimensions
