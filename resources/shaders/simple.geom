#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "common.h"

layout (triangles) in;
layout (triangle_strip, max_vertices = 5) out;

layout (push_constant) uniform params_t
{
    mat4 mProjView;
    mat4 mModel;
} params;

layout (location = 0) in VS_IN
{
    vec3 wPos;
    vec3 wNorm;
    vec3 wTangent;
    vec2 texCoord;
} vIn[];

layout (location = 0) out VS_OUT
{
    vec3 wPos;
    vec3 wNorm;
    vec3 wTangent;
    vec2 texCoord;
} vOut;

layout (binding = 0, set = 0) uniform AppData
{
    UniformParams Params;
};

void emit_vertex(vec3 pos, vec3 norm, vec3 tangent, vec2 texCoord)
{
    gl_Position = params.mProjView * vec4(pos, 1.0f);
    vOut.wPos = pos;
    vOut.wNorm = norm;
    vOut.wTangent = tangent;
    vOut.texCoord = texCoord;
    EmitVertex();
}

void main(void)
{
    float pike_scale = 0.05f; 
    vec3 triangleNorm = normalize(vIn[0].wNorm + vIn[1].wNorm + vIn[2].wNorm);
    vec3 avgVertexPos = (vIn[0].wPos + vIn[1].wPos + vIn[2].wPos) / 3.0f;
    vec3 avgVertexTangent = normalize(vIn[0].wTangent + vIn[1].wTangent + vIn[2].wTangent);
    vec2 avgVertexTexCoord = (vIn[0].texCoord + vIn[1].texCoord + vIn[2].texCoord) / 3.0f;
    avgVertexPos += pike_scale * (1 + sin(3*Params.time + 2*avgVertexPos.x)) * triangleNorm ;
    emit_vertex(vIn[0].wPos, vIn[0].wNorm, vIn[0].wTangent, vIn[0].texCoord);
    emit_vertex(vIn[1].wPos, vIn[1].wNorm, vIn[1].wTangent, vIn[1].texCoord);
    emit_vertex(avgVertexPos, triangleNorm, avgVertexTangent, avgVertexTexCoord);
    emit_vertex(vIn[2].wPos, vIn[2].wNorm, vIn[2].wTangent, vIn[2].texCoord);
    emit_vertex(vIn[0].wPos, vIn[0].wNorm, vIn[0].wTangent, vIn[0].texCoord);
    emit_vertex(avgVertexPos, triangleNorm, avgVertexTangent, avgVertexTexCoord);
    emit_vertex(vIn[1].wPos, vIn[1].wNorm, vIn[1].wTangent, vIn[1].texCoord);
    emit_vertex(vIn[2].wPos, vIn[2].wNorm, vIn[2].wTangent, vIn[2].texCoord);
    EndPrimitive();
}