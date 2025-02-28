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

struct MyFancyVertex
{
    float px, py, pz;  // Position
    float nx, ny, nz;  // Normal
    float tx, ty, tz;  // Tangent
    float bx, by, bz;  // Bitangent
    float u, v;        // Texture coordinates
};


static bgfx::VertexLayout g_vertexLayout;

static void initVertexLayout()
{
    // This layout must match the usage in varying.def.sc (POSITION, NORMAL0, TEXCOORD0).
    // We skip COLOR0, COLOR1 in the actual geometry data for brevity (we can fill them if needed).
    g_vertexLayout.begin()
            .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Tangent,   3, bgfx::AttribType::Float) // <--
            .add(bgfx::Attrib::Bitangent, 3, bgfx::AttribType::Float) // <--
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
                         std::vector<MyFancyVertex>& outVertices,
                         std::vector<uint32_t>& outIndices)
{
    if (!mesh) return;

    outVertices.clear();
    outIndices.clear();
    outVertices.reserve(mesh->mNumVertices);
    outIndices.reserve(mesh->mNumFaces * 3);

    bool hasNormals   = mesh->HasNormals();
    bool hasTexcoords = (mesh->mTextureCoords[0] != nullptr);
    bool hasTangents  = (mesh->HasTangentsAndBitangents());

    for (unsigned int i = 0; i < mesh->mNumVertices; i++)
    {
        MyFancyVertex v;

        // Position
        v.px = mesh->mVertices[i].x;
        v.py = mesh->mVertices[i].y;
        v.pz = mesh->mVertices[i].z;

        // Normal (default to up if missing)
        if (hasNormals)
        {
            v.nx = mesh->mNormals[i].x;
            v.ny = mesh->mNormals[i].y;
            v.nz = mesh->mNormals[i].z;
        }
        else
        {
            v.nx = 0.0f; v.ny = 1.0f; v.nz = 0.0f;
        }

        // Tangent & Bitangent (for normal mapping)
        if (hasTangents)
        {
            v.tx = mesh->mTangents[i].x;
            v.ty = mesh->mTangents[i].y;
            v.tz = mesh->mTangents[i].z;

            v.bx = mesh->mBitangents[i].x;
            v.by = mesh->mBitangents[i].y;
            v.bz = mesh->mBitangents[i].z;
        }
        else
        {
            // Generate dummy tangent space (not ideal but prevents crashes)
            v.tx = 1.0f; v.ty = 0.0f; v.tz = 0.0f;
            v.bx = 0.0f; v.by = 1.0f; v.bz = 0.0f;
        }

        // Texture Coordinates (UVs)
        if (hasTexcoords)
        {
            v.u = mesh->mTextureCoords[0][i].x;
            v.v = mesh->mTextureCoords[0][i].y;
        }
        else
        {
            v.u = 0.0f; v.v = 0.0f;
        }

        outVertices.push_back(v);
    }

    // Load indices
    for (unsigned int f = 0; f < mesh->mNumFaces; f++)
    {
        const aiFace& face = mesh->mFaces[f];
        if (face.mNumIndices == 3)  // Ensure it's a triangle
        {
            outIndices.push_back(face.mIndices[0]);
            outIndices.push_back(face.mIndices[1]);
            outIndices.push_back(face.mIndices[2]);
        }
    }

    std::cout << "num vertices:" << outVertices.size() << std::endl;
    std::cout << "num indices:" << outIndices.size() << std::endl;
}


bgfx::TextureHandle loadTextureType(const aiMaterial* mat, aiTextureType type, const aiScene* scene)
{
    aiString path;
    if (mat->GetTexture(type, 0, &path) == AI_SUCCESS)
    {
        // This is similar to your existing code. We'll do the same logic:
        if (path.length > 0 && path.data[0] == '*')
        {
            // Embedded texture
            int texIndex = atoi(path.C_Str() + 1);
            if (texIndex >= 0 && texIndex < (int)scene->mNumTextures)
            {
                const aiTexture* aiTex = scene->mTextures[texIndex];
                if (aiTex && aiTex->mHeight == 0)
                {
                    // Compressed texture in memory (e.g. PNG, JPG data)
                    int x, y, n;
                    unsigned char* data = stbi_load_from_memory(
                                reinterpret_cast<const stbi_uc*>(aiTex->pcData),
                                aiTex->mWidth,
                                &x, &y, &n, 4
                                );
                    if (data)
                    {
                        // Only swap Red and Blue if this is a Diffuse/BaseColor texture.
                        //if (type == aiTextureType_DIFFUSE || type == aiTextureType_BASE_COLOR)
                        {
                            for (int i = 0; i < x * y * 4; i += 4)
                            {
                                std::swap(data[i], data[i + 2]); // Swap Red and Blue channels
                            }
                        }

                        const bgfx::Memory* mem = bgfx::copy(data, x * y * 4);
                        stbi_image_free(data);

                        return bgfx::createTexture2D(
                                    uint16_t(x),
                                    uint16_t(y),
                                    false, 1,
                                    bgfx::TextureFormat::BGRA8, 0,
                                    mem
                                    );
                    }
                }
            }
        }
        else
        {
            // External file reference (not embedded).
            // You'd have to load from disk. For glTF, often the textures might be separate files.
            // ...
        }
    }

    return BGFX_INVALID_HANDLE;
}

// Converts Assimp texture types to human-readable strings
const char* aiTextureTypeToString(aiTextureType type)
{
    switch (type)
    {
    case aiTextureType_DIFFUSE: return "Diffuse (Albedo/BaseColor)";
    case aiTextureType_SPECULAR: return "Specular";
    case aiTextureType_AMBIENT: return "Ambient";
    case aiTextureType_EMISSIVE: return "Emissive";
    case aiTextureType_HEIGHT: return "Height Map (Bump)";
    case aiTextureType_NORMALS: return "Normal Map";
    case aiTextureType_SHININESS: return "Shininess (Roughness/Gloss)";
    case aiTextureType_OPACITY: return "Opacity";
    case aiTextureType_DISPLACEMENT: return "Displacement";
    case aiTextureType_LIGHTMAP: return "Lightmap";
    case aiTextureType_REFLECTION: return "Reflection";
    case aiTextureType_BASE_COLOR: return "Base Color (PBR)";
    case aiTextureType_NORMAL_CAMERA: return "Normal Map (PBR)";
    case aiTextureType_EMISSION_COLOR: return "Emission (PBR)";
    case aiTextureType_METALNESS: return "Metallic (PBR)";
    case aiTextureType_DIFFUSE_ROUGHNESS: return "Roughness (PBR)";
    case aiTextureType_AMBIENT_OCCLUSION: return "AO (PBR)";
    case aiTextureType_UNKNOWN: return "Unknown Type";
    default: return "Other";
    }
}

void printMaterialTextures(const aiScene* scene)
{
    if (!scene || scene->mNumMaterials == 0)
    {
        std::cout << "No materials found in the scene.\n";
        return;
    }

    std::cout << "Textures in this .glb file:\n";

    for (unsigned int m = 0; m < scene->mNumMaterials; ++m)
    {
        const aiMaterial* material = scene->mMaterials[m];

        for (int type = aiTextureType_NONE; type <= aiTextureType_UNKNOWN; ++type)
        {
            int textureCount = material->GetTextureCount((aiTextureType)type);
            if (textureCount > 0)
            {
                std::cout << "Material " << m << " has " << textureCount << " texture(s) of type: "
                          << aiTextureTypeToString((aiTextureType)type) << "\n";

                for (int i = 0; i < textureCount; i++)
                {
                    aiString path;
                    if (material->GetTexture((aiTextureType)type, i, &path) == AI_SUCCESS)
                    {
                        std::cout << "  - " << path.C_Str() << " (Type: " << aiTextureTypeToString((aiTextureType)type) << ")\n";
                    }
                }
            }
        }
    }
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
                "/dev/shm/drill/glb/Drill_01_1k.glb",
                aiProcess_Triangulate |
                aiProcess_GenNormals |
                aiProcess_CalcTangentSpace |
                //aiProcess_FlipUVs //|              // <-- Flips V texture coordinates
                aiProcess_ConvertToLeftHanded     // <-- Converts to left-handed coordinate system
                );

    if (!scene)
    {
        std::cerr << "Failed to load scene via Assimp: " << importer.GetErrorString() << std::endl;
        return -1;
    }

    //debug:
    printMaterialTextures(scene);

    // For simplicity, assume the scene has at least one mesh
    if (scene->mNumMeshes < 1)
    {
        std::cerr << "No meshes found in the scene." << std::endl;
        return -1;
    }

    // Grab the first mesh
    const aiMesh* mesh = scene->mMeshes[0];

    // Convert to our arrays
    std::vector<MyFancyVertex> vertices;
    std::vector<uint32_t> indices;
    assimpMeshToBuffers(mesh, vertices, indices);

    // Create static vertex/index buffers
    bgfx::VertexBufferHandle vbh = bgfx::createVertexBuffer(
                bgfx::makeRef(vertices.data(), sizeof(MyFancyVertex) * vertices.size()),
                g_vertexLayout
                );

    bgfx::IndexBufferHandle ibh = bgfx::createIndexBuffer(
                bgfx::makeRef(indices.data(), sizeof(uint32_t) * indices.size()),
                BGFX_BUFFER_INDEX32 // IMPORTANT: 32-bit indices
                );


    // Suppose we have one material
    const aiMaterial* mat = scene->mMaterials[0];

    bgfx::TextureHandle diffuseTex   = loadTextureType(mat, aiTextureType_DIFFUSE, scene);
    bgfx::TextureHandle normalTex    = loadTextureType(mat, aiTextureType_NORMALS, scene);
    bgfx::TextureHandle armTex = loadTextureType(mat, aiTextureType_UNKNOWN, scene);
    if (!bgfx::isValid(armTex))
    {
        std::cout << "Warning: No valid ARM (AO, Roughness, Metalness) texture found.\n";
    }


    if (!bgfx::isValid(diffuseTex)) {
        std::cerr << "Warning: No valid embedded texture found. The duck will be untextured." << std::endl;
    }
    if (!bgfx::isValid(normalTex)) {
        std::cerr << "Warning: No valid embedded normalTex texture found. The duck will be untextured normalTex." << std::endl;
    }


    // Create a sampler uniform so we can bind the texture in the fragment shader
    bgfx::UniformHandle s_texColor   = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
    bgfx::UniformHandle s_texNormal  = bgfx::createUniform("s_texNormal", bgfx::UniformType::Sampler);
    bgfx::UniformHandle s_texARM     = bgfx::createUniform("s_texARM", bgfx::UniformType::Sampler); // Replaces s_texMetal & s_texRough

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
            const bx::Vec3 at   = {0.0f, 0.1f, 0.0f};
            const bx::Vec3 eye  = {0.0f, 0.25f, 0.25f}; // Move slightly back so we can see the duck
            const bx::Vec3 up   = {0.0f, 1.0f, 0.0f};
            bx::mtxLookAt(view, eye, at, up);
        }

        float proj[16];
        {
            bx::mtxProj(proj, 60.0f, float(fbWidth)/float(fbHeight), 0.1f, 500.0f, bgfx::getCaps()->homogeneousDepth);
        }

        bgfx::setViewTransform(0, view, proj);

        float mtxRotateY[16];
        bx::mtxRotateY(mtxRotateY, time); // Keep Y-axis rotation

        float mtxTranslate[16];
        bx::mtxTranslate(mtxTranslate, 0.0f, 0.0f, 0.0f); // Adjust as needed

        float mtxModel[16];
        bx::mtxMul(mtxModel, mtxRotateY, mtxTranslate); // Only apply Y rotation

        // Submit the geometry
        bgfx::setTransform(mtxModel);
        bgfx::setVertexBuffer(0, vbh);
        bgfx::setIndexBuffer(ibh);

        // In your render loop, after setting up the transform, vertex, index buffers:
        if (bgfx::isValid(diffuseTex))  bgfx::setTexture(0, s_texColor, diffuseTex);
        if (bgfx::isValid(normalTex))   bgfx::setTexture(1, s_texNormal, normalTex);
        if (bgfx::isValid(armTex))
            bgfx::setTexture(2, s_texARM, armTex);


        bgfx::setState(
                    BGFX_STATE_WRITE_RGB |
                    BGFX_STATE_WRITE_Z   |
                    BGFX_STATE_DEPTH_TEST_LESS |
                    BGFX_STATE_CULL_CCW
                    );
        bgfx::submit(0, program);

        // Advance frame
        bgfx::frame();
        //glfwSwapBuffers(window);
    }

    // Cleanup
    bgfx::destroy(program);
    bgfx::destroy(vbh);
    bgfx::destroy(ibh);

    if (bgfx::isValid(armTex))
        bgfx::destroy(armTex);
    if (bgfx::isValid(normalTex))
        bgfx::destroy(normalTex);
    if (bgfx::isValid(diffuseTex))
        bgfx::destroy(diffuseTex);

    bgfx::destroy(s_texColor);
    bgfx::destroy(s_texNormal);
    bgfx::destroy(s_texARM);

    bgfx::shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
