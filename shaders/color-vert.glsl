#version 450

layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec3 v_col;

layout(binding = 0) uniform ubo_t{
    mat4 model;
} ubo;

layout(location = 0) out vec3 f_col;

void main()
{
    gl_Position = ubo.model * vec4(v_pos, 1.0);
    f_col = v_col;
}
