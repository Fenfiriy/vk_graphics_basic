#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "common.h"

layout (location = 0) out vec4 out_fragColor;

layout (location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

layout (binding = 0, set = 0) uniform AppData
{
  UniformParams Params;
};

float gamma = 2.2;

layout (binding = 1) uniform sampler2D hdrImage;

// https://www.shadertoy.com/view/lslGzl  whitePreservingLumaBasedReinhardToneMapping
vec4 tone_mapping(vec3 color)
{
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
	float toneMappedLuma = luma / (1. + luma);
	color *= toneMappedLuma / luma;
	color = pow(color, vec3(1. / gamma));
	return vec4(color, 1.);
}

void main()
{
  const vec4 hdrColor = texture(hdrImage, surf.texCoord);
  if (Params.toneMapping)
  {
    out_fragColor = tone_mapping(vec3(hdrColor));    
  }
  else
  {
    out_fragColor = clamp(hdrColor, vec4(0.0f), vec4(1.0f));
  }
}