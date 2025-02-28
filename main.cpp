#include <cstdio>
#include <vector>
#include <iostream>
#include <cmath>

// Assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// BGFX / BX
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>

// GLFW
#include "GLFW/glfw3.h"
#define GLFW_EXPOSE_NATIVE_X11
#include "GLFW/glfw3native.h"

// stb_image for decoding the embedded texture
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// -----------------------------------------------------------------------------
// For convenience, we’ll define a structure for our vertex data.
// We’ll match the layout used in varying.def.sc: position, normal, texcoord0
// We won't explicitly store colors in the model data, but we'll fill them in
// as dummy if needed.
// -----------------------------------------------------------------------------
struct PosNormalTexcoordVertex
{
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

static bgfx::VertexLayout g_vertexLayout;

static void initVertexLayout()
{
    // This layout must match the usage in varying.def.sc (POSITION, NORMAL0, TEXCOORD0).
    // We skip COLOR0, COLOR1 in the actual geometry data for brevity (we can fill them if needed).
    g_vertexLayout.begin()
        .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
    .end();
}

// -----------------------------------------------------------------------------
// Load a .bin shader file compiled by the bgfx shader compiler
// -----------------------------------------------------------------------------
bgfx::ShaderHandle loadShader(const char* _name)
{
    // Read the file:
    FILE* fp = fopen(_name, "rb");
    if (!fp)
    {
        std::cerr << "Could not open shader file: " << _name << std::endl;
        return BGFX_INVALID_HANDLE;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    const bgfx::Memory* mem = bgfx::alloc(uint32_t(size + 1));
    fread(mem->data, 1, size, fp);
    fclose(fp);

    // Null-terminate for safety (though not strictly required by bgfx)
    mem->data[size] = '\0';

    // Create the shader
    bgfx::ShaderHandle handle = bgfx::createShader(mem);
    bgfx::setName(handle, _name);
    return handle;
}

// -----------------------------------------------------------------------------
// Utility to create a bgfx program from vertex and fragment shader .bin
// -----------------------------------------------------------------------------
bgfx::ProgramHandle loadProgram(const char* _vsName, const char* _fsName)
{
    bgfx::ShaderHandle vsh = loadShader(_vsName);
    bgfx::ShaderHandle fsh = loadShader(_fsName);
    return bgfx::createProgram(vsh, fsh, true /* destroy shaders when program is destroyed */);
}

// -----------------------------------------------------------------------------
// Convert Assimp mesh data -> our geometry buffers
// -----------------------------------------------------------------------------
void assimpMeshToBuffers(const aiMesh* mesh,
                         std::vector<PosNormalTexcoordVertex> &outVertices,
                         std::vector<uint32_t>                &outIndices)
{
    outVertices.reserve(mesh->mNumVertices);
    // If we have no normals or UVs, fill them with something safe
    bool hasNormals  = (mesh->mNormals != nullptr);
    bool hasTexcoords= (mesh->mTextureCoords[0] != nullptr);

    for (unsigned int i = 0; i < mesh->mNumVertices; i++)
    {
        PosNormalTexcoordVertex v;

        // Position
        v.px = mesh->mVertices[i].x;
        v.py = mesh->mVertices[i].y;
        v.pz = mesh->mVertices[i].z;

        // Normal
        if (hasNormals)
        {
            v.nx = mesh->mNormals[i].x;
            v.ny = mesh->mNormals[i].y;
            v.nz = mesh->mNormals[i].z;
        }
        else
        {
            v.nx = 0.0f;
            v.ny = 1.0f;
            v.nz = 0.0f;
        }

        // Texcoord
        if (hasTexcoords)
        {
            v.u = mesh->mTextureCoords[0][i].x;
            v.v = mesh->mTextureCoords[0][i].y;
        }
        else
        {
            v.u = 0.0f;
            v.v = 0.0f;
        }

        outVertices.push_back(v);
    }

    // Indices
    for (unsigned int f = 0; f < mesh->mNumFaces; f++)
    {
        const aiFace& face = mesh->mFaces[f];
        // We assume triangle faces
        if (face.mNumIndices == 3)
        {
            outIndices.push_back(face.mIndices[0]);
            outIndices.push_back(face.mIndices[1]);
            outIndices.push_back(face.mIndices[2]);
        }
    }
}

// -----------------------------------------------------------------------------
// Attempt to find & load the first embedded diffuse texture we find
// Returns a valid bgfx::TextureHandle or BGFX_INVALID_HANDLE if none is found
// -----------------------------------------------------------------------------
bgfx::TextureHandle loadEmbeddedTexture(const aiScene* scene)
{
    // We'll loop over materials and check for a diffuse texture.
    // If it's embedded, we'll decode with stb_image from memory.
    for (unsigned int m = 0; m < scene->mNumMaterials; m++)
    {
        aiString path;
        if (scene->mMaterials[m]->GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS)
        {
            // path may be "*0", "*1", etc. for embedded textures
            if (path.length > 0 && path.data[0] == '*')
            {
                int texIndex = atoi(path.C_Str() + 1); // skip '*'
                if (texIndex >= 0 && texIndex < (int)scene->mNumTextures)
                {
                    const aiTexture* aiTex = scene->mTextures[texIndex];
                    if (aiTex && aiTex->mHeight == 0)
                    {
                        // This means it's an *embedded compressed* texture in memory (e.g. PNG/JPG bytes).
                        int x, y, n;
                        unsigned char* data = stbi_load_from_memory(
                            reinterpret_cast<const stbi_uc*>(aiTex->pcData),
                            aiTex->mWidth, // size in bytes
                            &x, &y, &n, 4
                        );

                        if (data)
                        {
                            // Convert RGBA -> BGRA (swap R and B)
                            for (int i = 0; i < x * y * 4; i += 4)
                            {
                                std::swap(data[i], data[i + 2]); // Swap Red and Blue
                            }

                            // Create a bgfx texture
                            const bgfx::Memory* mem = bgfx::copy(data, x*y*4);
                            stbi_image_free(data);

                            bgfx::TextureHandle th = bgfx::createTexture2D(
                                uint16_t(x),
                                uint16_t(y),
                                false,
                                1,
                                bgfx::TextureFormat::BGRA8,
                                0,
                                mem
                            );
                            return th;
                        }
                    }
                    else if (aiTex && aiTex->mHeight > 0)
                    {
                        // This means it's *embedded raw pixels*, which is less common for glTF.
                        // If this happens, you'd need to interpret aiTex->mWidth, mHeight, etc. directly.
                        // Not typical for GLB, so we’ll skip it here.
                    }
                }
            }
        }
    }
    // If we get here, we didn't find anything
    return BGFX_INVALID_HANDLE;
}

// -----------------------------------------------------------------------------
int main(int argc, char** argv)
{
    // -------------------------------------------------------------------------
    // Initialize GLFW
    // -------------------------------------------------------------------------
    if (!glfwInit())
    {
        std::cerr << "Failed to init GLFW." << std::endl;
        return -1;
    }

    // We only need an OpenGL context so bgfx can hook into it
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Duck Example", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Failed to create GLFW window." << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    // -------------------------------------------------------------------------
    // Initialize bgfx (using the OpenGL renderer)
    // -------------------------------------------------------------------------
    bgfx::PlatformData pd;
    memset(&pd, 0, sizeof(pd));

    // On desktop, for bgfx + GLFW with OpenGL, we need the native window handle/context
#if defined(_WIN32)
    pd.nwh = glfwGetWin32Window(window);
#elif defined(__APPLE__)
    pd.nwh = glfwGetCocoaWindow(window);
#else
    // For X11, e.g.:
    pd.nwh = (void*)(uintptr_t)glfwGetX11Window(window);
#endif

    bgfx::Init init;
    init.type = bgfx::RendererType::OpenGL; // Force OpenGL
    init.platformData = pd;
    init.resolution.width  = 1280;
    init.resolution.height = 720;
    init.resolution.reset  = BGFX_RESET_VSYNC;
    bgfx::init(init);

    // Set the view clear color (cornflower blue, for instance)
    bgfx::setViewClear(0,
                       BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                       0x6495EDff, // ABGR
                       1.0f,       // depth
                       0           // stencil
    );

    // -------------------------------------------------------------------------
    // Prepare geometry and load the duck
    // -------------------------------------------------------------------------
    initVertexLayout();

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        "/dev/shm/duckGLTF/duckGltfExport.glb",
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_CalcTangentSpace //|
        //aiProcess_FlipUVs //|              // <-- Flips V texture coordinates
        //aiProcess_ConvertToLeftHanded     // <-- Converts to left-handed coordinate system
    );

    if (!scene)
    {
        std::cerr << "Failed to load scene via Assimp: " << importer.GetErrorString() << std::endl;
        return -1;
    }

    // For simplicity, assume the scene has at least one mesh
    if (scene->mNumMeshes < 1)
    {
        std::cerr << "No meshes found in the scene." << std::endl;
        return -1;
    }

    // Grab the first mesh
    const aiMesh* mesh = scene->mMeshes[0];

    // Convert to our arrays
    std::vector<PosNormalTexcoordVertex> vertices;
    std::vector<uint32_t> indices;
    assimpMeshToBuffers(mesh, vertices, indices);

    // Create static vertex/index buffers
    bgfx::VertexBufferHandle vbh = bgfx::createVertexBuffer(
        bgfx::makeRef(vertices.data(), sizeof(PosNormalTexcoordVertex)*vertices.size()),
        g_vertexLayout
    );
    bgfx::IndexBufferHandle ibh = bgfx::createIndexBuffer(
        bgfx::makeRef(indices.data(), sizeof(uint32_t)*indices.size()), BGFX_BUFFER_INDEX32
    );

    // Try to load texture from embedded data
    bgfx::TextureHandle textureHandle = loadEmbeddedTexture(scene);
    if (!bgfx::isValid(textureHandle))
    {
        std::cerr << "Warning: No valid embedded texture found. The duck will be untextured." << std::endl;
    }

    // Create a sampler uniform so we can bind the texture in the fragment shader
    bgfx::UniformHandle s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

    // Load the duck shaders
    bgfx::ProgramHandle program = loadProgram("vs_duck.bin", "fs_duck.bin");
    if (!bgfx::isValid(program))
    {
        std::cerr << "Could not create program. Exiting." << std::endl;
        return -1;
    }

    // -------------------------------------------------------------------------
    // Main loop
    // -------------------------------------------------------------------------
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    if (fbWidth < 1) fbWidth = 1;
    if (fbHeight < 1) fbHeight = 1;

    bgfx::reset((uint32_t)fbWidth, (uint32_t)fbHeight, BGFX_RESET_VSYNC);

    bgfx::setViewRect(0, 0, 0, (uint16_t)fbWidth, (uint16_t)fbHeight);

    float time = 0.0f;
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // Update time and do a rotation
        double currentTime = glfwGetTime();
        time = float(currentTime);

        // Set up a simple camera
        float view[16];
        {
            const bx::Vec3 at   = {0.0f, 0.0f, 0.0f};
            const bx::Vec3 eye  = {100.0f, 5.0f, 100.0f}; // Move slightly back so we can see the duck
            const bx::Vec3 up   = {0.0f, 1.0f, 0.0f};
            bx::mtxLookAt(view, eye, at, up);
        }

        float proj[16];
        {
            bx::mtxProj(proj, 60.0f, float(fbWidth)/float(fbHeight), 0.1f, 500.0f, bgfx::getCaps()->homogeneousDepth);
        }

        bgfx::setViewTransform(0, view, proj);

        // Build a model matrix that rotates the duck around Y
        float mtxRotate[16];
        bx::mtxRotateY(mtxRotate, time);

        // Optionally translate it a bit so it's visible
        float mtxTranslate[16];
        bx::mtxTranslate(mtxTranslate, 0.0f, 0.0f, 0.0f);

        float mtxModel[16];
        bx::mtxMul(mtxModel, mtxRotate, mtxTranslate);

        // Submit the geometry
        bgfx::setTransform(mtxModel);
        bgfx::setVertexBuffer(0, vbh);
        bgfx::setIndexBuffer(ibh);

        if (bgfx::isValid(textureHandle))
        {
            // Bind texture to stage 0
            bgfx::setTexture(0, s_texColor, textureHandle);
        }
        else
        {
            std::cerr << "INVALID TEXTURE HANDLE" << std::endl;
        }

        // Submit draw call
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_CULL_CCW);
        bgfx::submit(0, program);

        // Advance frame
        bgfx::frame();
        //glfwSwapBuffers(window);
    }

    // Cleanup
    bgfx::destroy(program);
    bgfx::destroy(vbh);
    bgfx::destroy(ibh);
    if (bgfx::isValid(textureHandle))
        bgfx::destroy(textureHandle);
    bgfx::destroy(s_texColor);

    bgfx::shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
