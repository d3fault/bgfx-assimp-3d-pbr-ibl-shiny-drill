$input v_texcoord0, v_tbn

#include <bgfx_shader.sh>

// Samplers
SAMPLER2D(s_texColor, 0);   // Diffuse color
SAMPLER2D(s_texNormal, 1);  // Normal map
SAMPLER2D(s_texARM, 2);     // Metallic & Roughness packed

void main()
{
    vec4 baseColor = texture2D(s_texColor, v_texcoord0);
    vec3 normalMap = texture2D(s_texNormal, v_texcoord0).rgb;

    vec4 armTex = texture2D(s_texARM, v_texcoord0);
    float ao       = armTex.r; // Ambient Occlusion (if needed)
    float roughness = armTex.g; // Roughness is usually in Green
    float metallic  = armTex.b; // Metallic is usually in Blue
    //roughness = clamp(roughness, 0.3, 0.9);

    // Convert normal map from [0,1] to [-1,1]
    normalMap = normalMap * 2.0 - 1.0;

    // Convert normal from tangent space to world space
    vec3 N = normalize(v_tbn * normalMap);

    // Fake simple lighting (for debugging)
    vec3 lightDir = normalize(vec3(0.3, 0.7, 0.5));
    float lambert = max(dot(N, lightDir), 0.0);
    

    // Apply fake shading
    vec3 diffuse  = baseColor.rgb * lambert;
    //vec3 finalColor = diffuse * (1.0 - metallic) + vec3(1.0) * metallic * lambert;
    //vec3 finalColor = diffuse * (1.0 - metallic * 0.8) + vec3(1.0) * metallic * 0.2;
    vec3 finalColor = diffuse * (1.0 - metallic * 0.2) + vec3(1.0) * metallic * 0.2;


    gl_FragColor = vec4(finalColor, baseColor.a);
    
    //debug diffuse:
    //gl_FragColor = vec4(baseColor.rgb, 1.0);

    //debug roughness:
    //gl_FragColor = vec4(vec3(roughness), 1.0);
}
