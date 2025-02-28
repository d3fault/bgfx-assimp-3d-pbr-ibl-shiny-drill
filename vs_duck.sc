$input a_position, a_normal, a_tangent, a_bitangent, a_texcoord0
$output v_texcoord0, v_tbn

#include <bgfx_shader.sh>

//uniform mat4 u_modelViewProj;

void main()
{
    // Standard transform
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));

    // Pass UV
    v_texcoord0 = a_texcoord0;

    // Normalize T, B, N just in case
    vec3 T = normalize(a_tangent);
    vec3 B = normalize(a_bitangent);
    vec3 N = normalize(a_normal);

    // Build a 3x3 TBN for use in the fragment shader
    // We'll store it row-wise or column-wise (we just have to be consistent).
    v_tbn = mat3(T, B, N);
}
