$input v_texcoord0, v_tbn, v_worldPos

#include <bgfx_shader.sh>

// Base PBR textures
SAMPLER2D(s_texColor, 0);    // Diffuse (Base Color)
SAMPLER2D(s_texNormal, 1);   // Normal Map
SAMPLER2D(s_texARM,    2);   // Packed AO (R), Roughness (G), Metallic (B)

// IBL textures
SAMPLERCUBE(s_irradiance, 3);
SAMPLERCUBE(s_radiance,   4);
SAMPLER2D(s_brdfLUT,      5);

// Camera position in world space
uniform vec4 u_camPos;

void main()
{
    // -------------------------------------------------------------
    // 1) Fetch the base PBR maps
    // -------------------------------------------------------------
    vec4 baseColor = texture2D(s_texColor, v_texcoord0);
    vec3 normalMap = texture2D(s_texNormal, v_texcoord0).rgb;
    vec4 arm = texture2D(s_texARM, v_texcoord0);

    float ao = arm.r;
    float roughness = arm.g;
    float metallic = arm.b;

    // Convert normal map from [0,1] to [-1,1]
    // For example, a typical "tangent-space normal" decode:
    normalMap = normalMap * 2.0 - 1.0;

    // Transform normal from tangent-space to world-space using v_tbn
    // You can apply a normal strength factor if you want, but let's keep it 1.0
    vec3 N = normalize(v_tbn * normalMap);

    // -------------------------------------------------------------
    // 2) Compute base PBR parameters
    //    We treat "baseColor" as the albedo for non-metals,
    //    and the F0 color for metals. 
    // -------------------------------------------------------------
    vec3 albedo = baseColor.rgb;

    // Fresnel reflectance at normal incidence (F0)
    // For metals: F0 = albedo
    // For non-metals: F0 = 0.04
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // -------------------------------------------------------------
    // 3) Compute view direction, reflection direction, etc
    // -------------------------------------------------------------
    vec3 V = normalize(u_camPos.xyz - v_worldPos);
    vec3 R = reflect(-V, N);

    float NdotV = max(dot(N, V), 0.0);

    // -------------------------------------------------------------
    // 4) Direct Lighting (optional but nice to have)
    // -------------------------------------------------------------
    // Just a single directional light for demonstration:
    vec3 lightDir = normalize(vec3(0.3, 0.7, 0.5));
    vec3 L = lightDir;
    float NdotL = max(dot(N, L), 0.0);

    // Simple Lambert for direct diffuse
    vec3 directDiffuse = albedo * NdotL;

    // Simple Blinn-Phong for direct spec (not physically correct but simpler)
    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float specPower = mix(8.0, 128.0, 1.0 - roughness);
    float directSpecularFactor = pow(NdotH, specPower);

    // Compute Schlick’s Fresnel factor per light direction
    float fresnelL = pow(1.0 - max(dot(H, V), 0.0), 5.0);
    vec3 F_L = F0 + (1.0 - F0) * fresnelL;
    vec3 directSpecular = directSpecularFactor * F_L;

    // Combine direct lighting
    // You can multiply by a light color and intensity if you like
    vec3 directLight = (directDiffuse + directSpecular) * NdotL;

    // -------------------------------------------------------------
    // 5) IBL: Diffuse from irradiance
    // -------------------------------------------------------------
    // This uses the normal to sample convolved environment
    vec3 diffuseIBL = textureCube(s_irradiance, N).rgb * albedo * ao;

    // -------------------------------------------------------------
    // 6) IBL: Specular from prefiltered environment map + BRDF LUT
    // -------------------------------------------------------------
    // For specular IBL with a prefiltered GGX environment, 
    // we need to pick the appropriate LOD based on roughness.
    // Typically: LOD = roughness * maxMips
    // For a quick hack, let's just do:
    float maxLod = 8.0; // depends on how many mip levels your s_radiance has
    float lod = roughness * maxLod;

    // Sample the prefiltered environment
    //vec3 prefilteredColor = textureCubeLodEXT(s_radiance, R, lod).rgb;
    vec3 prefilteredColor = textureLod(s_radiance, R, lod).rgb;

    // Now we get the BRDF integration terms from the LUT
    // The LUT is typically RG where x is the scale, y is the bias for the fresnel
    vec2 brdf = texture2D(s_brdfLUT, vec2(NdotV, roughness)).rg;

    // Final specular IBL
    // F is the fresnel color (which starts at F0 and increases at glancing angles)
    // but the LUT has integrated the geometry term, so we multiply prefilteredColor
    // by (F * brdf.x + brdf.y).
    // For a simple approach, we can treat the “fresnel” factor as just F0 if you prefer
    // or do the full schlick:
    // Schlick's approx: F = F0 + (1-F0)*pow(1 - NdotV, 5)
    // Let’s do a quick partial:
    float fresnel = pow(1.0 - NdotV, 5.0);
    vec3 F = F0 + (1.0 - F0) * fresnel;

    vec3 specularIBL = prefilteredColor * (F * brdf.x + brdf.y);

    // -------------------------------------------------------------
    // 7) Final combine: direct + IBL
    // -------------------------------------------------------------
    vec3 color = directLight + diffuseIBL + specularIBL;

    // Keep alpha from baseColor
    gl_FragColor = vec4(color, baseColor.a);
}
