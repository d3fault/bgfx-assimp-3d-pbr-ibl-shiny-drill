$input a_position
$output v_cubeCoord

#include <bgfx_shader.sh>

uniform mat4 u_viewMat;
uniform mat4 u_projMat;

void main()
{
    // Remove translation from the view matrix so the skybox remains centered on the camera.
    mat4 viewNoTrans = u_viewMat;
    viewNoTrans[3].xyz = vec3(0.0, 0.0, 0.0);

    // Transform the vertex by the view (without translation) and projection.
    vec4 worldPos = mul(viewNoTrans, vec4(a_position, 1.0));
    gl_Position = mul(u_projMat, worldPos);

    // Pass the view-space direction to the fragment shader.
    v_cubeCoord = worldPos.xyz;
}
