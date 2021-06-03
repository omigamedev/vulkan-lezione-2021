#version 450

layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec3 v_col;
layout(location = 2) in vec2 v_uvs;

layout(binding = 0) uniform ubo_t{
    mat4 model;
} ubo;

layout(location = 0) out vec3 f_col;
layout(location = 1) out vec2 f_uvs;

void main()
{
    gl_Position = ubo.model * vec4(v_pos, 1.0);
    f_col = v_col;
    f_uvs = v_uvs;
}
