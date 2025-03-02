// This file describes how vertex outputs map to fragment inputs in bgfx shader code.
// We keep some of the user’s sample fields, plus we add a "v_cubeCoord : TEXCOORD2"
// for sampling the cubemap in the fragment shader.

vec4 v_color0    : COLOR0    = vec4(1.0, 0.0, 0.0, 1.0);
vec4 v_color1    : COLOR1    = vec4(0.0, 1.0, 0.0, 1.0);
vec2 v_texcoord0 : TEXCOORD0 = vec2(0.0, 0.0);
vec3 v_normal    : TEXCOORD1 = vec3(0.0, 1.0, 0.0);

vec3 v_cubeCoord : TEXCOORD2 = vec3(0.0, 0.0, 0.0);

vec3 a_position  : POSITION;
vec3 a_normal    : NORMAL0;
vec4 a_color0    : COLOR0;
vec4 a_color1    : COLOR1;
vec2 a_texcoord0 : TEXCOORD0;
