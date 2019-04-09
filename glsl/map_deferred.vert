layout (location = 0) in vec2 pos;
layout (location = 1) in vec2 tex_pos;

out vec2 f_tex_pos;

void main() {
    gl_Position = vec4(pos, 0.0, 1.0);
    f_tex_pos = tex_pos;
}
