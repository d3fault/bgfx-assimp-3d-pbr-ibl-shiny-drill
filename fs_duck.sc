/*
  Demonstration of multi-texture usage (diffuse, normal, metallic, roughness).
  It's still "hacky lighting" (not physically based!). But it shows how
  to sample & combine these textures.
*/

$input v_texcoord0, v_tbn

#include <bgfx_shader.sh>

// Samplers
SAMPLER2D(s_texColor, 0);   // Diffuse color
SAMPLER2D(s_texNormal, 1);  // Normal map
SAMPLER2D(s_texMetal, 2);   // Metallic
SAMPLER2D(s_texRough, 3);   // Roughness

void main()
{
    // 1) Sample all textures:
    vec4 baseColor = texture2D(s_texColor, v_texcoord0);
    vec3 normalMap = texture2D(s_texNormal, v_texcoord0).rgb;
    float metallic = texture2D(s_texMetal, v_texcoord0).r;   // or .g or .b if needed
    float rough    = texture2D(s_texRough, v_texcoord0).r;   // likewise

    // 2) Reconstruct normal from normalMap: range [0..1] -> [-1..1]
    normalMap = normalMap * 2.0 - 1.0;

    // 3) Convert tangent-space normal to "object space"
    // v_tbn is mat3(T, B, N). We multiply TBN * normalMap
    // so that (0,0,1) = the original mesh normal
    vec3 N = normalize(v_tbn * normalMap);

    // 4) Hacky lighting: one directional light + some ambient
    vec3 lightDir = normalize(vec3(0.3, 0.7, 0.5));
    float lambert = max(dot(N, lightDir), 0.0);
    float ambient = 0.3;  // hack

    // 5) Fake specular factor using metallic & roughness
    //    This is *not* real PBR. It's just a demonstration.
    float invRough = 1.0 - rough; // lower roughness => bigger highlight
    float specPower = mix(8.0, 128.0, invRough); 
    // For spec highlight, we also need view direction (assuming camera at 0,0, +Z)
    // For brevity, let's hack the viewDir:
    vec3 viewDir = vec3(0.0, 0.0, -1.0); 
    vec3 halfDir = normalize(lightDir + viewDir);
    float specAngle = max(dot(N, halfDir), 0.0);
    float spec = pow(specAngle, specPower);

    // 6) Combine terms:
    //    Metallic = use baseColor for spec color,
    //    Non-metal = 0.04 as F0
    vec3 F0 = mix(vec3(0.04), baseColor.rgb, metallic);

    // Simple approximation
    vec3 diffuse  = baseColor.rgb * (lambert + ambient);
    vec3 specular = F0 * spec * lambert; // only reflect if light hits

    vec3 finalColor = diffuse + specular;

    gl_FragColor = vec4(finalColor, baseColor.a);
}

