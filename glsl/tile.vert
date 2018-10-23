#if   __VERSION__ == 100
attribute vec2 pos;
attribute vec2 tex_pos;

varying vec2 f_tex_pos;
#elif __VERSION__ == 330
in vec2 pos;
in vec2 tex_pos;

out vec2 f_tex_pos;
#endif

uniform vec2 scale;
uniform vec2 translation;
uniform vec2 tex_scale;

void main()
{
    gl_Position = vec4(pos * scale + translation, 0.0, 1.0);
    f_tex_pos = tex_pos * tex_scale; //TODO precalculate (geometry shader?)?
}
