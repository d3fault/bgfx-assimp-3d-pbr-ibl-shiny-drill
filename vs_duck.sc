/*
    Simple vertex shader in bgfx shader format.
    We read position and texcoord from the vertex, transform by u_modelViewProj,
    and pass the texcoord along to the fragment stage.

    Matches the user-provided snippet plus normal usage in "varying.def.sc".
*/

$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_normal

#include <bgfx_shader.sh>

//uniform mat4 u_modelViewProj;

void main()
{
    // Transform position
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));

    // Pass through texcoord
    v_texcoord0 = a_texcoord0;

    // Pass the normal forward if you'd like to do lighting
    // For a quick hack, just pass it untransformed. If you want
    // correct lighting, you'd transform by a normal matrix, etc.
    v_normal = a_normal;
}

