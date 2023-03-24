#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 normal;
layout(location = 1) out vec4 albedo;

layout(push_constant) uniform params_t
{
    mat4 mProjView;
    mat4 mModel;
    uint id_albedo;
} PushConstant;

layout (location = 0) in VS_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec3 wTangent;
  vec2 texCoord;
} surf;

void main()
{
  normal = vec4(surf.wNorm, 1.0);
  switch(PushConstant.id_albedo)
  {
  case 0:
    albedo = vec4(1.f, 0.0f, 1.f, 1.0f);
    break;
  case 1:
    albedo = vec4(0.f, 1.f, 1.f, 1.0f);
    break;
  case 2:
    albedo = vec4(1.f, 1.f, 0.f, 1.0f);
    break;
  case 3:
    albedo = vec4(0.0f, 0.0f, 1.f, 1.0f);
    break;
  case 4:
    albedo = vec4(0.0f, 1.00f, 0.f, 1.0f);
    break;
  case 5:
    albedo = vec4(1.0f, 0.0f, 0.0f, 1.0f);
    break;
  }
}