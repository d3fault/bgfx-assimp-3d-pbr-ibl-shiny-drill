$input a_position, a_normal, a_tangent, a_texcoord0
$output v_texcoord0, v_tbn, v_worldPos

#include <bgfx_shader.sh>

uniform mat4 u_myModelMatrix;
//uniform mat4 u_myModelViewProj;

void main()
{
    vec4 worldPos = mul(u_myModelMatrix, vec4(a_position, 1.0));
    v_worldPos = worldPos.xyz;

    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));

    v_texcoord0 = a_texcoord0;

    vec3 T = normalize(mul(u_myModelMatrix, a_tangent).xyz);
    vec3 N = normalize(mul(u_myModelMatrix, vec4(a_normal, 0.0)).xyz);
    vec3 B = cross(N, T) * (a_tangent.w != 0.0 ? a_tangent.w : 1.0);

    v_tbn = mat3(T, B, N);
}
