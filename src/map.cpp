//@CONSIDER new header include_glm
#define GLM_FORCE_PURE
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
//
#include <lux_shared/common.hpp>
#include <lux_shared/map.hpp>
#include <lux_shared/net/net_order.hpp>
#include <lux_shared/util/packer.hpp>
//
#include <rendering.hpp>
#include <db.hpp>
#include "map.hpp"

GLuint program;
GLuint tileset;
HashMap<ChkPos, Chunk, util::Packer<ChkPos>> chunks;

static void build_mesh(Chunk &chunk, ChkPos const &pos);

void map_init(MapAssets assets) {
    glm::mat4 model_matrix = {
        0.1f, 0.f, 0.f, 0.f,
        0.f, 0.13333f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f};

    program = load_program(assets.vert_path, assets.frag_path);
    glUseProgram(program);
    set_uniform("model", program, glUniformMatrix4fv,
                1, GL_FALSE, glm::value_ptr(model_matrix));
    Vec2U tileset_size;
    tileset = load_texture(assets.tileset_path, tileset_size);
    Vec2F tex_scale = (Vec2F)assets.tile_size / (Vec2F)tileset_size;
    set_uniform("tex_scale", program, glUniform2fv,
                1, glm::value_ptr(tex_scale));
}

void map_render() {
    //@TODO command list

    glUseProgram(program);
    glBindTexture(GL_TEXTURE_2D, tileset);

    for(auto const& chunk : chunks) {
        if(chunk.first.z != 0) continue;
        if(!chunk.second.mesh.is_built) {
            try_build_mesh(chunk.first);
            continue;
        }
        Chunk::Mesh const& mesh = chunk.second.mesh;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.g_vbo);
        glVertexAttribPointer(0, 2, GL_INT, GL_FALSE,
            sizeof(Chunk::Mesh::GVert),
            (void*)offsetof(Chunk::Mesh::GVert, pos));
        glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_FALSE,
            sizeof(Chunk::Mesh::GVert),
            (void*)offsetof(Chunk::Mesh::GVert, tex_pos));
        glBindBuffer(GL_ARRAY_BUFFER, mesh.l_vbo);
        glVertexAttribPointer(2, 3, GL_UNSIGNED_BYTE, GL_TRUE,
            sizeof(Chunk::Mesh::LVert),
            (void*)offsetof(Chunk::Mesh::LVert, col));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glDrawElements(GL_TRIANGLES, mesh.trig_count * 3,
                       Chunk::Mesh::INDEX_GL_TYPE, 0);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(2);
    }
}

bool is_chunk_loaded(ChkPos const& pos) {
    return chunks.count(pos) > 0;
}

void load_chunk(NetServerSignal::MapLoad::Chunk const& net_chunk) {
    ChkPos pos = net_order(net_chunk.pos);
    LUX_LOG("loading chunk");
    LUX_LOG("    pos: {%zd, %zd, %zd}", pos.x, pos.y, pos.z);
    if(is_chunk_loaded(pos)) {
        LUX_LOG("chunk already loaded, ignoring it");
        return;
    }
    Chunk& chunk = chunks[pos];
    for(Uns i = 0; i < CHK_VOL; ++i) {
        chunk.voxels[i]     = net_order(net_chunk.voxels[i]);
        chunk.light_lvls[i] = net_order(net_chunk.light_lvls[i]);
    }
}

Chunk const& get_chunk(ChkPos const& pos) {
    LUX_ASSERT(is_chunk_loaded(pos));
    return chunks.at(pos);
}

void try_build_mesh(ChkPos const& pos) {
    constexpr ChkPos offsets[8] =
        {{-1, -1,  0}, {-1,  0,  0}, {-1,  1,  0}, { 0, -1,  0},
         { 0,  1,  0}, { 1, -1,  0}, { 1,  0,  0}, { 1,  1,  0}};

    if(!is_chunk_loaded(pos)) return;

    Chunk &chunk = chunks.at(pos);

    if(chunk.mesh.is_built) return;

    for(auto const& offset : offsets) {
        //@TODO request required chunks
        if(!is_chunk_loaded(pos + offset)) return;
    }
    build_mesh(chunk, pos);
}

//@CONSIDER replacing stuff with arrays
static void build_mesh(Chunk &chunk, ChkPos const &pos) {
    static DynArr<Chunk::Mesh::GVert> g_verts;
    static DynArr<Chunk::Mesh::LVert> l_verts;
    static DynArr<Chunk::Mesh::Idx>  idxs;

    g_verts.clear();
    l_verts.clear();
    idxs.clear();

    //@TODO this should be done only once
    g_verts.reserve(CHK_VOL * 4);
    l_verts.reserve(CHK_VOL * 4);
    idxs.reserve(CHK_VOL * 6);

    constexpr MapPos quad[4] =
         {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}};
    constexpr Vec2U tex_positions[4] =
        {{0, 0}, {0, 1}, {1, 1}, {1, 0}};

    Chunk::Mesh& mesh = chunk.mesh;
    Chunk::Mesh::Idx idx_offset = 0;

    auto get_voxel = [&] (MapPos const &pos) -> VoxelId
    {
        //@TODO use current chunk to reduce to_chk_* calls, and chunks access
        return chunks[to_chk_pos(pos)].voxels[to_chk_idx(pos)];
    };

    for(ChkIdx i = 0; i < CHK_VOL; ++i) {
        MapPos map_pos = to_map_pos(pos, i);
        VoxelType vox_type = db_voxel_type(get_voxel(map_pos));
        bool is_solid = db_voxel_type(chunk.voxels[i]).shape != VoxelType::EMPTY;
        if(!is_solid) continue; //@TODO
        for(U32 j = 0; j < 4; ++j) {
            constexpr MapPos vert_offsets[4] =
                {{-1, -1, 0}, {-1, 1, 0}, {1, -1, 0}, {1, 1, 0}};
            Vec3<U8> col_avg(0xFF, 0xFF, 0xFF);
            /*for(auto const &vert_offset : vert_offsets) {
                MapPos v_off_pos = map_pos + vert_offset;
                LightLvl light_lvl =
                    get_chunk(to_chk_pos(v_off_pos)).light_lvls[to_chk_idx(v_off_pos)];
                col_avg += Vec3F(
                (F32)((light_lvl & 0xF000) >> 12) / 15.f,
                (F32)((light_lvl & 0x0F00) >>  8) / 15.f,
                (F32)((light_lvl & 0x00F0) >>  4) / 15.f);
            }
            col_avg /= 4.f;*/
            Chunk::Mesh::GVert& g_vert = g_verts.emplace_back();
            g_vert.pos = map_pos + quad[j];
            g_vert.tex_pos = vox_type.tex_pos + tex_positions[j];
            Chunk::Mesh::LVert& l_vert = l_verts.emplace_back();
            l_vert.col = col_avg;
        }
        for(auto const &idx : {0, 1, 2, 2, 3, 0}) {
            idxs.emplace_back(idx + idx_offset);
        }
        idx_offset += 4;
    }
    glGenBuffers(1, &mesh.g_vbo);
    glGenBuffers(1, &mesh.l_vbo);
    glGenBuffers(1, &mesh.ebo);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Chunk::Mesh::GVert) * g_verts.size(),
                 g_verts.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.l_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Chunk::Mesh::LVert) * l_verts.size(),
                 l_verts.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(Chunk::Mesh::Idx) * idxs.size(),
                 idxs.data(), GL_STATIC_DRAW);
    mesh.is_built = true;
    mesh.trig_count = idxs.size() / 3; //@TODO
}
