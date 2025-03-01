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

#include <boost/beast/core/detail/base64.hpp>
#include <string>

struct MyFancyVertex
{
    float px, py, pz;  // Position
    float nx, ny, nz;  // Normal
    float tx, ty, tz, tw;  // Tangent (includes handedness)
    float u, v;        // Texture coordinates
};


static bgfx::VertexLayout g_vertexLayout;

// Simple cube geometry for the skybox (8 vertices, 36 indices for a textured box).
static float s_skyboxVertices[] =
{
    // x,    y,    z
    -10.0f,  10.0f, -10.0f,
    10.0f,   10.0f, -10.0f,
    10.0f,   10.0f,  10.0f,
    -10.0f,  10.0f,  10.0f,
    -10.0f, -10.0f, -10.0f,
    10.0f,  -10.0f, -10.0f,
    10.0f,  -10.0f,  10.0f,
    -10.0f, -10.0f,  10.0f,
};

static const uint16_t s_skyboxIndices[] =
{
    // top
    0, 1, 2,
    2, 3, 0,
    // bottom
    4, 6, 5,
    6, 4, 7,
    // front
    0, 4, 1,
    1, 4, 5,
    // back
    2, 6, 3,
    3, 6, 7,
    // left
    0, 3, 4,
    4, 3, 7,
    // right
    1, 5, 2,
    2, 5, 6
};

static bgfx::VertexBufferHandle s_skyboxVertBuffer = BGFX_INVALID_HANDLE;
static bgfx::IndexBufferHandle s_skyboxIndexBuffer = BGFX_INVALID_HANDLE;
static bgfx::ProgramHandle s_skyboxProgram = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle s_skyboxUniform    = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle s_uView     = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle s_uProj     = BGFX_INVALID_HANDLE;
static bgfx::TextureHandle s_skyboxTexture    = BGFX_INVALID_HANDLE;

// A simple structure for your skybox vertex
struct SkyboxVertex
{
    float x, y, z;
};

// Load file into bgfx memory buffer
static const bgfx::Memory* loadMem(const char* filename)
{
    FILE* fp = fopen(filename, "rb");
    if (!fp)
    {
        std::cerr << "Could not open file: " << filename << std::endl;
        return nullptr;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    const bgfx::Memory* mem = bgfx::alloc(size + 1);
    fread(mem->data, 1, size, fp);
    fclose(fp);

    mem->data[size] = '\0';
    return mem;
}

// Load a compiled shader (e.g. vs_skybox.bin, fs_skybox.bin)
static bgfx::ShaderHandle loadShader(const char* fpath)
{
    const bgfx::Memory* mem = loadMem(fpath);
    if (!mem)
    {
        return BGFX_INVALID_HANDLE;
    }
    return bgfx::createShader(mem);
}

static bgfx::TextureHandle loadTexture(const char* filePath)
{
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;

    // Open the texture file.
    FILE* fp = fopen(filePath, "rb");
    if (!fp)
    {
        std::cerr << "Could not open texture: " << filePath << std::endl;
        return handle;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0)
    {
        std::cerr << "Texture file is empty: " << filePath << std::endl;
        fclose(fp);
        return handle;
    }

    // Read the file data.
    uint8_t* data = new uint8_t[size];
    if (fread(data, 1, size, fp) != (size_t)size)
    {
        std::cerr << "Failed to read texture file: " << filePath << std::endl;
        fclose(fp);
        delete[] data;
        return handle;
    }
    fclose(fp);

    // Create a bgfx memory reference and upload it as a texture.
    const bgfx::Memory* mem = bgfx::copy(data, size);
    delete[] data; // Safe to delete now, because bgfx owns the memory.

    handle = bgfx::createTexture(mem);
    bgfx::setName(handle, filePath);

    if (!bgfx::isValid(handle))
    {
        std::cerr << "Failed to create texture from file: " << filePath << std::endl;
    }

    return handle;
}

static void initVertexLayout()
{
    // This layout must match the usage in varying.def.sc (POSITION, NORMAL0, TEXCOORD0).
    // We skip COLOR0, COLOR1 in the actual geometry data for brevity (we can fill them if needed).
    g_vertexLayout.begin()
        .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Tangent,   4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
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

        // Tangent
        if (hasTangents)
        {
            v.tx = mesh->mTangents[i].x;
            v.ty = mesh->mTangents[i].y;
            v.tz = mesh->mTangents[i].z;
        }
        else
        {
            // Generate dummy tangent space (not ideal but prevents crashes)
            v.tx = 1.0f; v.ty = 0.0f; v.tz = 0.0f;
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

static bgfx::TextureHandle createBgfxTextureFromMemory(const unsigned char* imageData, size_t dataSize)
{
    if (!imageData || dataSize == 0)
    {
        std::cerr << "[createBgfxTextureFromMemory] Invalid data or size.\n";
        return BGFX_INVALID_HANDLE;
    }

    int width, height, channels;
    // Use STBI_rgb_alpha to force 4 channels
    unsigned char* decoded = stbi_load_from_memory(
                imageData,
                static_cast<int>(dataSize),
                &width,
                &height,
                &channels,
                STBI_rgb_alpha
                );

    if (!decoded)
    {
        std::cerr << "[createBgfxTextureFromMemory] stbi_load_from_memory failed.\n";
        return BGFX_INVALID_HANDLE;
    }

    // STB outputs RGBA; BGFX expects BGRA by default.
    // Swap R/B in-place:
    for (int i = 0; i < width * height * 4; i += 4)
    {
        std::swap(decoded[i + 0], decoded[i + 2]); // R <-> B
    }

    const bgfx::Memory* mem = bgfx::copy(decoded, width * height * 4);
    stbi_image_free(decoded);

    // Create the BGFX texture
    bgfx::TextureHandle handle = bgfx::createTexture2D(
                static_cast<uint16_t>(width),
                static_cast<uint16_t>(height),
                false,     // no mipmaps
                1,         // number of layers
                bgfx::TextureFormat::BGRA8,
                0,
                mem
                );

    if (!bgfx::isValid(handle))
    {
        std::cerr << "[createBgfxTextureFromMemory] Failed to create BGFX texture.\n";
    }
    return handle;
}

static bgfx::TextureHandle loadEmbeddedTexture(const aiScene* scene, int texIndex)
{
    if (!scene || texIndex < 0 || texIndex >= static_cast<int>(scene->mNumTextures))
    {
        std::cerr << "[loadEmbeddedTexture] Invalid texture index.\n";
        return BGFX_INVALID_HANDLE;
    }

    const aiTexture* aiTex = scene->mTextures[texIndex];
    if (!aiTex)
    {
        std::cerr << "[loadEmbeddedTexture] aiTexture is null.\n";
        return BGFX_INVALID_HANDLE;
    }

    // If mHeight == 0, it's likely a compressed image in memory (PNG/JPG)
    if (aiTex->mHeight == 0)
    {
        // aiTex->pcData is actually raw compressed data
        // The size is stored in mWidth
        const unsigned char* rawData = reinterpret_cast<const unsigned char*>(aiTex->pcData);
        size_t dataSize = static_cast<size_t>(aiTex->mWidth);

        return createBgfxTextureFromMemory(rawData, dataSize);
    }
    else
    {
        // If mHeight != 0, this means it's an uncompressed (e.g. RGBA) array:
        // size = mWidth * mHeight * bytesPerPixel
        // But it's very unusual in GLTF/GLB. You can handle it similarly:
        size_t dataSize = size_t(aiTex->mWidth * aiTex->mHeight * 4); // 4 is a guess for RGBA
        const unsigned char* rawData = reinterpret_cast<const unsigned char*>(aiTex->pcData);
        return createBgfxTextureFromMemory(rawData, dataSize);
    }
}

static bgfx::TextureHandle loadExternalTexture(const std::string& filePath)
{
    // Example: simple file load into memory, then decode
    // (You might need a more robust file I/O system.)
    FILE* fp = fopen(filePath.c_str(), "rb");
    if (!fp)
    {
        std::cerr << "[loadExternalTexture] Failed to open file: " << filePath << "\n";
        return BGFX_INVALID_HANDLE;
    }

    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fileSize <= 0)
    {
        std::cerr << "[loadExternalTexture] Invalid file size: " << filePath << "\n";
        fclose(fp);
        return BGFX_INVALID_HANDLE;
    }

    std::vector<unsigned char> buffer(fileSize);
    fread(buffer.data(), 1, fileSize, fp);
    fclose(fp);

    // Now decode
    return createBgfxTextureFromMemory(buffer.data(), buffer.size());
}



static std::vector<unsigned char> base64Decode(const std::string& base64Str) {
    std::vector<unsigned char> decoded(boost::beast::detail::base64::decoded_size(base64Str.size()));
    auto result = boost::beast::detail::base64::decode(decoded.data(), base64Str.data(), base64Str.size());
    decoded.resize(result.first);  // Resize to actual output size
    return decoded;
}


// A helper if your path is "data:image/png;base64,XXX..."
static bgfx::TextureHandle loadBase64Texture(const std::string& base64Uri)
{
    // Typically the URI is something like "data:image/png;base64,iVBORw0KGgoAAAANSUhE..."
    // We need to find the comma that separates the header from the data
    size_t commaPos = base64Uri.find(',');
    if (commaPos == std::string::npos)
    {
        std::cerr << "[loadBase64Texture] Invalid data URI.\n";
        return BGFX_INVALID_HANDLE;
    }

    std::string base64Part = base64Uri.substr(commaPos + 1);

    // decode
    std::vector<unsigned char> rawBytes = base64Decode(base64Part);
    if (rawBytes.empty())
    {
        std::cerr << "[loadBase64Texture] Base64 decode failed.\n";
        return BGFX_INVALID_HANDLE;
    }

    return createBgfxTextureFromMemory(rawBytes.data(), rawBytes.size());
}


bgfx::TextureHandle loadTextureType(const aiMaterial* mat, aiTextureType type, const aiScene* scene)
{
    aiString aiPath;
    if (mat->GetTexture(type, 0, &aiPath) != AI_SUCCESS)
    {
        // No texture found for this type on this material
        return BGFX_INVALID_HANDLE;
    }

    std::string path = aiPath.C_Str();
    if (path.empty())
    {
        // Path is empty, no texture
        return BGFX_INVALID_HANDLE;
    }

    // Check if it's referencing embedded texture data like "*0", "*1", etc.
    // (Assimp uses '*' prefix for embedded index references.)
    if (path[0] == '*')
    {
        int texIndex = std::stoi(path.substr(1));
        return loadEmbeddedTexture(scene, texIndex);
    }
    // Check if it's a data URI (rare in .gltf)
    else if (path.rfind("data:", 0) == 0)
    {
        // We have data embedded in the path as base64
        return loadBase64Texture(path);
    }
    else
    {
        // Otherwise, assume it's an external file reference
        // path is relative to the .gltf/.glb; you may need to prepend the model directory
        return loadExternalTexture(path);
    }

    // Shouldn't get here, but if we do:
    std::cerr << "[loadTextureType] Unknown texture path format.\n";
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


    // Create vertex layout
    bgfx::VertexLayout skyboxVertLayout;
    skyboxVertLayout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .end();

    // Create vertex buffer
    s_skyboxVertBuffer = bgfx::createVertexBuffer(
                bgfx::makeRef(s_skyboxVertices, sizeof(s_skyboxVertices)),
                skyboxVertLayout
                );

    // Create index buffer
    s_skyboxIndexBuffer = bgfx::createIndexBuffer(
                bgfx::makeRef(s_skyboxIndices, sizeof(s_skyboxIndices))
                );

    // Load shaders
    bgfx::ShaderHandle vsh = loadShader("vs_skybox.bin");
    bgfx::ShaderHandle fsh = loadShader("fs_skybox.bin");
    s_skyboxProgram = bgfx::createProgram(vsh, fsh, true);

    // Create uniforms
    s_skyboxUniform = bgfx::createUniform("s_skyMap", bgfx::UniformType::Sampler);
    s_uView  = bgfx::createUniform("u_viewMat",   bgfx::UniformType::Mat4);
    s_uProj  = bgfx::createUniform("u_projMat",   bgfx::UniformType::Mat4);

    // Load cubemap KTX
    // Example: /dev/shm/mossyForestExr/skybox.ktx
    s_skyboxTexture = loadTexture("skybox.ktx");

    const int viewId_Skybox = 0;
    const int viewId_Mesh = 1;

    // Set the view clear color (cornflower blue, for instance)
    bgfx::setViewClear(viewId_Skybox,
                       BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                       0x6495EDff, // ABGR
                       1.0f,       // depth
                       0           // stencil
                       );
    bgfx::setViewClear(viewId_Mesh, BGFX_CLEAR_DEPTH);

    // -------------------------------------------------------------------------
    // Prepare geometry and load the duck
    // -------------------------------------------------------------------------
    initVertexLayout();

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
                //"Drill_01_1k.glb"
                "Drill_01_1k.gltf",
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
    bgfx::TextureHandle irradianceTex = loadTexture("irradiance.ktx");
    bgfx::TextureHandle brdfLutTex    = loadTexture("brdf_lut.ktx");


    if (!bgfx::isValid(armTex))
    {
        std::cerr << "Warning: No valid ARM (AO, Roughness, Metalness) texture found.\n";
        return 1;
    }
    if (!bgfx::isValid(diffuseTex)) {
        std::cerr << "Warning: No valid embedded texture found. The duck will be untextured." << std::endl;
        return 1;
    }
    if (!bgfx::isValid(normalTex)) {
        std::cerr << "Warning: No valid embedded normalTex texture found. The duck will be untextured normalTex." << std::endl;
        return 1;
    }
    if (!bgfx::isValid(irradianceTex)) {
        std::cerr << "Warning: invalid irradianceTex handle." << std::endl;
        return 1;
    }
    if (!bgfx::isValid(brdfLutTex)) {
        std::cerr << "Warning: invalid brdfLutTex handle." << std::endl;
        return 1;
    }



    // Create uniforms
    bgfx::UniformHandle u_myModelMatrix         = bgfx::createUniform("u_myModelMatrix",         bgfx::UniformType::Mat4);
    //bgfx::UniformHandle u_myModelViewProj = bgfx::createUniform("u_myModelViewProj", bgfx::UniformType::Mat4);
    bgfx::UniformHandle u_camPos        = bgfx::createUniform("u_camPos",        bgfx::UniformType::Vec4);

    // Create a sampler uniform so we can bind the texture in the fragment shader
    bgfx::UniformHandle s_texColor   = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
    bgfx::UniformHandle s_texNormal  = bgfx::createUniform("s_texNormal", bgfx::UniformType::Sampler);
    bgfx::UniformHandle s_texARM     = bgfx::createUniform("s_texARM", bgfx::UniformType::Sampler); // Replaces s_texMetal & s_texRough

    bgfx::UniformHandle s_irradiance = bgfx::createUniform("s_irradiance", bgfx::UniformType::Sampler);
    bgfx::UniformHandle s_radiance   = bgfx::createUniform("s_radiance",   bgfx::UniformType::Sampler);
    bgfx::UniformHandle s_brdfLUT    = bgfx::createUniform("s_brdfLUT",    bgfx::UniformType::Sampler);

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

    bgfx::setViewRect(viewId_Skybox, 0, 0, (uint16_t)fbWidth, (uint16_t)fbHeight);
    bgfx::setViewRect(viewId_Mesh, 0, 0, (uint16_t)fbWidth, (uint16_t)fbHeight);

    float time = 0.0f;
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // Update time and do a rotation
        double currentTime = glfwGetTime();
        time = float(currentTime);

        // Set up a simple camera
        float view[16];
        const bx::Vec3 at   = {0.0f, 0.1f, 0.0f};
        const bx::Vec3 eye  = {0.0f, 0.25f, 0.25f}; // Move slightly back so we can see the duck
        const bx::Vec3 up   = {0.0f, 1.0f, 0.0f};
        bx::mtxLookAt(view, eye, at, up);


        float proj[16];
        {
            bx::mtxProj(proj, 60.0f, float(fbWidth)/float(fbHeight), 0.1f, 500.0f, bgfx::getCaps()->homogeneousDepth);
        }


        //Skybox
        float viewNoTrans[16];
        bx::memCopy(viewNoTrans, view, sizeof(viewNoTrans));
        viewNoTrans[12] = 0.0f;
        viewNoTrans[13] = 0.0f;
        viewNoTrans[14] = 0.0f;
        float viewProjNoTrans[16];
        bx::mtxMul(viewProjNoTrans, viewNoTrans, proj);
        bgfx::setViewTransform(viewId_Skybox, viewNoTrans, viewProjNoTrans);
        // Set uniforms for the skybox vertex shader
        bgfx::setUniform(s_uView, view);
        bgfx::setUniform(s_uProj, proj);

        // Submit the skybox draw
        bgfx::setVertexBuffer(0, s_skyboxVertBuffer);
        bgfx::setIndexBuffer(s_skyboxIndexBuffer);

        // Bind the cubemap
        bgfx::setTexture(0, s_skyboxUniform, s_skyboxTexture);

        bgfx::setState(BGFX_STATE_WRITE_RGB);

        bgfx::submit(viewId_Skybox, s_skyboxProgram);




        //Drill mesh
        bgfx::setViewTransform(viewId_Mesh, view, proj);

        float mtxRotateY[16];
        bx::mtxRotateY(mtxRotateY, time);

        float mtxTranslate[16];
        bx::mtxTranslate(mtxTranslate, 0.0f, 0.0f, 0.0f);

        float mtxModel[16];
        bx::mtxMul(mtxModel, mtxRotateY, mtxTranslate);

        // Compute Model-View-Projection (MVP) matrix
        float modelViewProj[16];
        bx::mtxMul(modelViewProj, mtxModel, view); // Multiply Model * View
        bx::mtxMul(modelViewProj, modelViewProj, proj); // Multiply result * Projection
        bgfx::setTransform(modelViewProj);

        // Set shader uniforms
        bgfx::setUniform(u_myModelMatrix, mtxModel);
        //bgfx::setUniform(u_myModelViewProj, modelViewProj);

        // Use the `eye` position as `u_camPos`
        float camPos[4] = { eye.x, eye.y, eye.z, 0.0f };
        bgfx::setUniform(u_camPos, camPos);

        // Submit the geometry
        bgfx::setTransform(mtxModel);
        bgfx::setVertexBuffer(0, vbh);
        bgfx::setIndexBuffer(ibh);

        if (bgfx::isValid(diffuseTex)) bgfx::setTexture(0, s_texColor, diffuseTex);
        else std::cerr << "Error: diffuseTex (s_texColor) is invalid!" << std::endl;

        if (bgfx::isValid(normalTex)) bgfx::setTexture(1, s_texNormal, normalTex);
        else std::cerr << "Error: normalTex (s_texNormal) is invalid!" << std::endl;

        if (bgfx::isValid(armTex)) bgfx::setTexture(2, s_texARM, armTex);
        else std::cerr << "Error: armTex (s_texARM) is invalid!" << std::endl;

        if (bgfx::isValid(irradianceTex)) bgfx::setTexture(3, s_irradiance, irradianceTex);
        else std::cerr << "Error: irradianceTex (s_irradiance) is invalid!" << std::endl;

        if (bgfx::isValid(s_skyboxTexture)) bgfx::setTexture(4, s_radiance, s_skyboxTexture);
        else std::cerr << "Error: s_skyboxTexture (s_radiance) is invalid!" << std::endl;

        if (bgfx::isValid(brdfLutTex)) bgfx::setTexture(5, s_brdfLUT, brdfLutTex);
        else std::cerr << "Error: brdfLutTex (s_brdfLUT) is invalid!" << std::endl;

#if 1 //opaque
        bgfx::setState(
            BGFX_STATE_WRITE_RGB |
            BGFX_STATE_WRITE_Z |
            BGFX_STATE_DEPTH_TEST_LESS |
            BGFX_STATE_CULL_CCW |           // Culls backfaces (CCW is standard)
            BGFX_STATE_MSAA                // Enables anti-aliasing (optional, if MSAA is supported)
        );
#else
        bgfx::setState(
            BGFX_STATE_WRITE_RGB |
            BGFX_STATE_WRITE_Z |
            BGFX_STATE_DEPTH_TEST_LESS |
            BGFX_STATE_CULL_CCW |
            BGFX_STATE_MSAA |
            BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA) // Standard alpha blending
        );
#endif

        bgfx::submit(viewId_Mesh, program);

        // Advance frame
        bgfx::frame();
        //glfwSwapBuffers(window);
    }

    // Cleanup
    bgfx::destroy(program);
    bgfx::destroy(vbh);
    bgfx::destroy(ibh);

    // Destroy uniforms
    bgfx::destroy(u_myModelMatrix);
    //bgfx::destroy(u_myModelViewProj);
    bgfx::destroy(u_camPos);

    if (bgfx::isValid(armTex))
        bgfx::destroy(armTex);
    if (bgfx::isValid(normalTex))
        bgfx::destroy(normalTex);
    if (bgfx::isValid(diffuseTex))
        bgfx::destroy(diffuseTex);
    if (bgfx::isValid(irradianceTex)) bgfx::destroy(irradianceTex);
    if (bgfx::isValid(brdfLutTex))    bgfx::destroy(brdfLutTex);

    bgfx::destroy(s_texColor);
    bgfx::destroy(s_texNormal);
    bgfx::destroy(s_texARM);
    bgfx::destroy(s_irradiance);
    bgfx::destroy(s_radiance);
    bgfx::destroy(s_brdfLUT);

    bgfx::destroy(s_skyboxTexture);
    bgfx::destroy(s_skyboxUniform);
    bgfx::destroy(s_uView);
    bgfx::destroy(s_uProj);

    bgfx::destroy(s_skyboxIndexBuffer);
    bgfx::destroy(s_skyboxVertBuffer);
    bgfx::destroy(s_skyboxProgram);

    bgfx::shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
