#version 450
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 0) out vec3 out_color;
layout(binding = 0) uniform Frame {
    mat4 vp;
    mat4 model;
    mat4 nmat;
    vec4 color;
    vec4 sun;
} frame;
void main() {
    vec4 world = frame.model * vec4(in_pos, 1.0);
    gl_Position = frame.vp * world;
    vec3 n = normalize((frame.nmat * vec4(in_normal, 0.0)).xyz);
    float light = 0.25 + 0.75 * abs(dot(n, frame.sun.xyz));
    out_color = frame.color.rgb * light;
}
