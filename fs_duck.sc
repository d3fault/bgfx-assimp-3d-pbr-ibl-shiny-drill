$input v_texcoord0, v_tbn, v_worldPos

#include <bgfx_shader.sh>

// Base PBR textures
SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_texNormal, 1);
SAMPLER2D(s_texARM, 2);

// IBL textures
SAMPLERCUBE(s_irradiance, 3);
SAMPLERCUBE(s_radiance, 4);
SAMPLER2D(s_brdfLUT, 5);

// Camera position in world space
uniform vec4 u_camPos;

void main()
{
    vec4 baseColor = texture2D(s_texColor, v_texcoord0);
    vec3 normalMap = texture2D(s_texNormal, v_texcoord0).rgb;
    vec4 arm = texture2D(s_texARM, v_texcoord0);

    float ao = arm.r;
    float roughness = clamp(arm.g, 0.3, 1.0);
    float metallic = arm.b;

    normalMap = normalMap * 2.0 - 1.0;
    vec3 N = normalize(v_tbn * normalMap);
    vec3 albedo = baseColor.rgb;

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 V = normalize(u_camPos.xyz - v_worldPos);
    vec3 R = reflect(-V, N);
    float NdotV = max(dot(N, V), 0.0);

    vec3 lightDir = normalize(vec3(0.3, 0.7, 0.5));
    vec3 L = lightDir;
    float NdotL = max(dot(N, L), 0.0);

    vec3 directDiffuse = albedo * NdotL;

    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);

    // Cook-Torrance specular BRDF
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float D = alpha2 / (3.14159 * pow((NdotH * NdotH * (alpha2 - 1.0) + 1.0), 2.0));

    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float G = NdotL / (NdotL * (1.0 - k) + k) * NdotV / (NdotV * (1.0 - k) + k);

    // Compute Schlick’s Fresnel factor per light direction
    float fresnelL = pow(1.0 - max(dot(H, V), 0.0), 5.0);
    vec3 F_L = F0 + (1.0 - F0) * fresnelL;

    vec3 directSpecular = (D * G * F_L) / (4.0 * NdotL * NdotV + 0.0001);
    vec3 directLight = (directDiffuse + directSpecular) * NdotL;

    // Adjust Diffuse IBL Intensity slightly more
    float iblIntensity = mix(0.03, 0.25, pow(roughness, 3.0));
    vec3 diffuseIBL = textureCube(s_irradiance, N).rgb * albedo * ao * iblIntensity;

    // Reduce IBL contrast slightly more
    diffuseIBL = pow(diffuseIBL, vec3(1.3));

    // IBL Specular from prefiltered environment map + BRDF LUT
    float maxLod = 8.0;
    float lod = roughness * maxLod;
    vec3 prefilteredColor = textureLod(s_radiance, R, lod).rgb;
    vec2 brdf = texture2D(s_brdfLUT, vec2(NdotV, roughness)).rg;
    float fresnel = pow(1.0 - NdotV, 5.0);
    vec3 F = F0 + (1.0 - F0) * fresnel;
    vec3 specularIBL = prefilteredColor * (F * brdf.x + brdf.y);

    // Final combine: direct + IBL
    vec3 color = directLight + diffuseIBL + specularIBL;
    gl_FragColor = vec4(color, baseColor.a);
}
