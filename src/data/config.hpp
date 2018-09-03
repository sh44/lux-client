#pragma once

#include <glm/glm.hpp>
//
#include <lux/alias/scalar.hpp>
#include <lux/alias/string.hpp>
#include <lux/net/port.hpp>
#include <lux/alias/vec_2.hpp>

namespace data
{

class Database;

struct Config
{
    Database const *db;
    String    vert_shader_path;
    String    frag_shader_path;
    String    interface_shader_path;
    String    tileset_path;
    String    font_path;
    Vec2UI window_size;
    Vec2UI window_scale;
    Vec2UI tile_size;
    Vec2UI char_size;
    U32       char_scale;
    struct
    {
        String hostname;
        net::Port port;
    } server;
    F32 view_range;
    F32 load_range;
    F32 fov;
    glm::vec4 sky_color;
    String   client_name;
};

}
