#version 450

layout(location = 0) in vec3 f_col;
layout(location = 0) out vec4 frag;

void main()
{
    frag = vec4(f_col, 1.0);
}
