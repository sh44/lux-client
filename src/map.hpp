#pragma once

#include <set>
//
#include <render/gl.hpp>
//
#include <lux/alias/hash_map.hpp>
#include <lux/alias/set.hpp>
#include <lux/common/map.hpp>
//
#include <map/chunk.hpp>

namespace data { struct Database; }
namespace net::server { struct Chunk; }
namespace map { struct Tile; }

class Map
{
    public:
    Map(data::Database const &db);

    map::Tile  const *get_tile(MapPos const &pos) const;
    map::Chunk const *get_chunk(ChkPos const &pos) const;

    void add_chunk(net::server::Chunk const &new_chunk);
    private:

    HashMap<ChkPos, map::Chunk> chunks;
    data::Database const &db;
};