$input v_texcoord0, v_tbn

#include <bgfx_shader.sh>

// Samplers
SAMPLER2D(s_texColor, 0);   // Diffuse (Base Color)
SAMPLER2D(s_texNormal, 1);  // Normal Map
SAMPLER2D(s_texARM, 2);     // Packed Ambient Occlusion, Roughness, Metallic

void main()
{
    // Sample textures
    vec4 baseColor = texture2D(s_texColor, v_texcoord0);
    vec3 normalMap = texture2D(s_texNormal, v_texcoord0).rgb;
    vec4 armTex = texture2D(s_texARM, v_texcoord0);

    // Extract PBR values
    float ao = armTex.r;       // Ambient Occlusion (if used)
    float roughness = armTex.g; // Roughness (Green channel)
    float metallic = armTex.b;  // Metallic (Blue channel)

    // Convert normal map from [0,1] to [-1,1] and reduce strength slightly
    normalMap = mix(vec3(0.0, 0.0, 1.0), normalMap * 2.0 - 1.0, 0.5);
    vec3 N = normalize(v_tbn * normalMap);

    // Lighting Setup
    vec3 lightDir = normalize(vec3(0.3, 0.7, 0.5)); // Simple directional light
    float lambert = max(dot(N, lightDir), 0.0);     // Lambertian diffuse term

    // Fake reflection boost (simulating indirect light)
    vec3 halfDir = normalize(lightDir + vec3(0.0, 0.0, 1.0)); // Fake reflection
    float specFactor = pow(max(dot(N, halfDir), 0.0), 16.0);

    // Add soft ambient lighting
    float ambient = 0.3;
    float lightFactor = lambert + ambient + specFactor * 0.3;

    // Final Color Calculation
    vec3 finalColor = baseColor.rgb * (1.0 - metallic * 0.8) * lightFactor 
                      + vec3(1.0) * metallic * 0.1;

    gl_FragColor = vec4(finalColor, baseColor.a);

    //debug normals:
    //gl_FragColor = vec4(N * 0.5 + 0.5, 1.0);
}
