#include <lux/net/server/tick.hpp>
#include <iostream>
//
#include <render/interface_vertex.hpp>
#include <render/index.hpp>
#include <data/config.hpp>
#include <renderer.hpp>
#include "debug_interface.hpp"

DebugInterface::DebugInterface(GLFWwindow *win, Renderer &renderer,
                               data::Config const &conf) :
    IoNode(win),
    char_size(conf.char_size),
    renderer(renderer)
{
    program.init(conf.interface_shader_path + ".vert",
                 conf.interface_shader_path + ".frag");
    program.use();
    Vec2<U32> font_size = font.load(conf.font_path);
    font_char_size = font_size / char_size;
    glm::vec2 font_scale = glm::vec2(1.f, 1.f) / (glm::vec2)font_char_size;
    program.set_uniform("tex_scale", glUniform2f, font_scale.x, font_scale.y);

    glGenBuffers(1, &vbo_id);
    glGenBuffers(1, &ebo_id);

    Vec2<I32> screen_size_temp;
    glfwGetWindowSize(win, &screen_size_temp.x, &screen_size_temp.y);
    screen_size = screen_size_temp;
}

void DebugInterface::take_key(I32 key, I32 code, I32 action, I32 mods)
{
    (void)code;
    (void)mods;
    if(key == GLFW_KEY_Y && action == GLFW_PRESS)
    {
        renderer.toggle_wireframe();
    }
    else if(key == GLFW_KEY_U && action == GLFW_PRESS)
    {
        renderer.toggle_face_culling();
    }
    else if(key == GLFW_KEY_I && action == GLFW_PRESS)
    {
        renderer.increase_view_range();
    }
    else if(key == GLFW_KEY_O && action == GLFW_PRESS)
    {
        renderer.decrease_view_range();
    }
}

void DebugInterface::take_st(net::server::Tick const &st)
{
    program.use();
    font.use();
    //TODO optimize this, use buffer instead of separate draw calls?
    render_text("x: " + std::to_string(st.player_pos.x), {0, 0});
    render_text("y: " + std::to_string(st.player_pos.y), {0, 1});
    render_text("z: " + std::to_string(st.player_pos.z), {0, 2});
    render_text("y - toggle wireframe mode", {-1, 0});
    render_text("u - toggle face culling  ", {-1, 1});
    render_text("i - increase view range  ", {-1, 2});
    render_text("o - decrease view range  ", {-1, 3});
}

void DebugInterface::take_resize(Vec2<U32> const &size)
{
    screen_size = size;
}

void DebugInterface::render_text(String const &str, Vec2<I32> const &base_pos)
{
    Vector<render::InterfaceVertex> vertices;
    Vector<render::Index>           indices;

    vertices.reserve(str.size() * 4);
    indices.reserve(str.size() * 6);

    Vec2<U32> pos = base_pos;
    Vec2<U32> screen_char_size = screen_size / char_size;
    if(base_pos.x < 0)
    {
        pos.x = screen_char_size.x - (std::abs(base_pos.x) + str.size());
    }
    if(base_pos.y < 0)
    {
        pos.y = screen_char_size.y - std::abs(base_pos.y);
    }
    render::Index index_offset = 0;
    constexpr Vec2<U32> verts[4] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};

    for(auto const &character : str)
    {
        Vec2<U32> char_pos = get_char_pos(character);
        for(auto const &vert : verts)
        {
            Vec2<U32> vert_pos = pos + vert;
            glm::vec2 ndc_pos = (((glm::vec2)(vert_pos * char_size) /
                                (glm::vec2)screen_size) * 2.f) - glm::vec2(1, 1);
            ndc_pos.y = -ndc_pos.y;
            vertices.emplace_back(ndc_pos, (glm::vec2)(char_pos + vert));
        }
        for(auto const &idx : {0, 1, 2, 2, 1, 3})
        {
            indices.emplace_back(index_offset + idx);
        }
        index_offset += 4;
        pos.x += 1;
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo_id);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_id);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
            sizeof(render::InterfaceVertex),
            (void*)offsetof(render::InterfaceVertex, pos));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
            sizeof(render::InterfaceVertex),
            (void*)offsetof(render::InterfaceVertex, tex_pos));

    glBufferData(GL_ARRAY_BUFFER,
                 sizeof(render::InterfaceVertex) * vertices.size(),
                 vertices.data(),
                 GL_STREAM_DRAW);

    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 sizeof(render::Index) * indices.size(),
                 indices.data(),
                 GL_STREAM_DRAW);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawElements(GL_TRIANGLES, indices.size(), render::INDEX_TYPE, 0);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
}

Vec2<U32> DebugInterface::get_char_pos(char character)
{
    /* this works for the specific font that is currently used */
    U8 idx = character;
    return Vec2<U32>(idx % font_char_size.x, idx / font_char_size.x);
}
