$input v_cubeCoord

#include <bgfx_shader.sh>

// We’ll sample from a cubemap (KTX processed with texturec --equirect).
SAMPLERCUBE(s_skyMap, 0);

void main()
{
    // Normalize the direction and sample the cubemap.
    vec3 dir = normalize(v_cubeCoord);
    gl_FragColor = textureCube(s_skyMap, dir);
}
