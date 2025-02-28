$input v_texcoord0, v_normal

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
    vec3 normal = normalize(v_normal);
    float lighting = max(dot(normal, vec3(0.3, 0.7, 0.5)), 0.3); // Fake light direction

    vec4 texColor = texture2D(s_texColor, v_texcoord0);
    gl_FragColor = texColor * lighting; // Apply "fake" lighting
}
