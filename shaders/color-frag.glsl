#version 450

layout(binding = 1) uniform ubo_t
{
    vec4 tint;
} ubo;

layout(binding = 2) uniform sampler2D tex;

layout(location = 0) in vec3 f_col;
layout(location = 1) in vec2 f_uvs;

layout(location = 0) out vec4 frag;

void main()
{
    frag = texture(tex, f_uvs);
}
