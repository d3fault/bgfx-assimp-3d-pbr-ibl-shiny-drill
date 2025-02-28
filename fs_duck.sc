$input v_texcoord0, v_normal

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
    vec4 texColor = texture2D(s_texColor, v_texcoord0);
    
    // Hack: just brighten it
    gl_FragColor = texColor * 1.5;
}
