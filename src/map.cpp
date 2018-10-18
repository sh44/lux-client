#include <config.hpp>
//
#include <cstring>
//
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
//
#include <lux_shared/common.hpp>
#include <lux_shared/map.hpp>
#include <lux_shared/util/packer.hpp>
//
#include <rendering.hpp>
#include <db.hpp>
#include <client.hpp>
#include <viewport.hpp>
#include "map.hpp"

GLuint tile_program;
GLuint tileset;
GLuint light_program;

struct GeometryMesh {
    typedef U32 Idx;
    static constexpr GLenum IDX_GL_TYPE = GL_UNSIGNED_INT;
#pragma pack(push, 1)
    struct Vert {
        Vec2<U16> pos;
    };
#pragma pack(pop)
    GLuint vbo;
    GLuint ebo;
};

struct MatMesh {
#pragma pack(push, 1)
    struct Vert {
        Vec2<U16> tex_pos;
    };
#pragma pack(pop)
    GLuint vbo;
#if defined(LUX_GL_3_3)
    GLuint vao;
#endif
};

struct LightMesh {
    typedef U32 Idx;
    static constexpr GLenum IDX_GL_TYPE = GL_UNSIGNED_INT;
    static constexpr SizeT  IDX_NUM     = std::pow(CHK_SIZE - 1, 2) * 6;
#pragma pack(push, 1)
    struct Vert {
        Vec2<U16> pos;
        Vec3<U8>  col;
    };
#pragma pack(pop)
    GLuint vbo;
    GLuint ebo;
#if defined(LUX_GL_3_3)
    GLuint vao;
#endif
};

struct {
    GLint pos;
    GLint tex_pos;
} tile_shader_attribs;

struct {
    GLint pos;
    GLint col;
} light_shader_attribs;

static GeometryMesh       geometry_mesh;
VecMap<ChkPos, Chunk>     chunks;
VecMap<ChkPos, MatMesh>   mat_meshes;
VecMap<ChkPos, LightMesh> light_meshes;
VecSet<ChkPos>            chunk_requests;

static void build_mat_mesh(MatMesh& mesh, Chunk const& chunk);
static void build_light_mesh(LightMesh& mesh, Chunk const& chunk);

static void map_load_programs() {
    char const* tileset_path = "tileset.png";
    Vec2U const tile_size = {16, 16};
    tile_program = load_program("glsl/tile.vert", "glsl/tile.frag");
    Vec2U tileset_size;
    tileset = load_texture(tileset_path, tileset_size);
    Vec2F tex_scale = (Vec2F)tile_size / (Vec2F)tileset_size;

    glUseProgram(tile_program);
    tile_shader_attribs.pos     = glGetAttribLocation(tile_program, "pos");
    tile_shader_attribs.tex_pos = glGetAttribLocation(tile_program, "tex_pos");

    set_uniform("tex_scale", tile_program, glUniform2fv,
                1, glm::value_ptr(tex_scale));

    light_program = load_program("glsl/light.vert", "glsl/light.frag");
    glUseProgram(light_program);
    light_shader_attribs.pos = glGetAttribLocation(light_program, "pos");
    light_shader_attribs.col = glGetAttribLocation(light_program, "col");
}

void map_init() {
    map_load_programs();

    Arr<GeometryMesh::Vert, CHK_VOL * 4>     verts;
    Arr<GeometryMesh::Idx,  CHK_VOL * 2 * 3> idxs;

    for(ChkIdx i = 0; i < CHK_VOL; ++i) {
        IdxPos idx_pos = to_idx_pos(i);
        for(Uns j = 0; j < 4; ++j) {
            constexpr Vec2<U16> quad[4] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
            verts[i * 4 + j].pos = (Vec2<U16>)idx_pos + quad[j];
        }
        GeometryMesh::Idx constexpr idx_order[6] = {0, 1, 2, 2, 3, 1};
        for(Uns j = 0; j < 6; ++j) {
            idxs[i * 6 + j] = i * 4 + idx_order[j];
        }
    }
    glGenBuffers(1, &geometry_mesh.vbo);
    glGenBuffers(1, &geometry_mesh.ebo);

    glBindBuffer(GL_ARRAY_BUFFER, geometry_mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GeometryMesh::Vert) *
        CHK_VOL * 4, verts, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry_mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GeometryMesh::Idx) *
        CHK_VOL * 2 * 3, idxs, GL_STATIC_DRAW);
}

void map_render() {
    auto const& player_pos = last_player_pos;
    U32 constexpr RENDER_DIST = 2;

    static DynArr<ChkPos> render_list;
    render_list.reserve(std::pow(2 * RENDER_DIST - 1, 2));

    ChkPos center = to_chk_pos(player_pos);
    ChkPos iter;
    iter.z = center.z;
    for(iter.y  = center.y - RENDER_DIST;
        iter.y <= center.y + RENDER_DIST;
        iter.y++) {
        for(iter.x  = center.x - RENDER_DIST;
            iter.x <= center.x + RENDER_DIST;
            iter.x++) {
            if(is_chunk_loaded(iter)) render_list.emplace_back(iter);
            else                      chunk_requests.emplace(iter);
        }
    }

    glUseProgram(tile_program);
    set_uniform("scale", tile_program, glUniform2fv,
                1, glm::value_ptr(world_viewport.scale));
    glBindTexture(GL_TEXTURE_2D, tileset);
#if defined(LUX_GLES_2_0)
    glBindBuffer(GL_ARRAY_BUFFER, geometry_mesh.vbo);
    glVertexAttribPointer(tile_shader_attribs.pos,
        2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(GeometryMesh::Vert),
        (void*)offsetof(GeometryMesh::Vert, pos));
    glEnableVertexAttribArray(tile_shader_attribs.pos);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry_mesh.ebo);
#endif

    for(auto const& chk_pos : render_list) {
        Vec2F translation = world_viewport.pos +
            (Vec2F)(chk_pos * ChkPos(CHK_SIZE));
        set_uniform("translation", tile_program, glUniform2fv,
                    1, glm::value_ptr(translation));

        MatMesh const& mesh = mat_meshes.at(chk_pos);
#if defined(LUX_GLES_2_0)
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glVertexAttribPointer(tile_shader_attribs.tex_pos,
            2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(MatMesh::Vert),
            (void*)offsetof(MatMesh::Vert, tex_pos));
        glEnableVertexAttribArray(tile_shader_attribs.tex_pos);
#elif defined(LUX_GL_3_3)
        glBindVertexArray(mesh.vao);
#endif
        glDrawElements(GL_TRIANGLES, CHK_VOL * 2 * 3,
            GeometryMesh::IDX_GL_TYPE, 0);
#if defined(LUX_GLES_2_0)
        glDisableVertexAttribArray(tile_shader_attribs.tex_pos);
#endif
    }

#if defined(LUX_GLES_2_0)
    glDisableVertexAttribArray(tile_shader_attribs.pos);
#endif

    glUseProgram(light_program);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    set_uniform("scale", light_program, glUniform2fv,
                1, glm::value_ptr(world_viewport.scale));
    for(auto const& chk_pos : render_list) {
        Vec2F translation = world_viewport.pos +
            (Vec2F)(chk_pos * ChkPos(CHK_SIZE));
        set_uniform("translation", light_program, glUniform2fv,
                    1, glm::value_ptr(translation));

        LightMesh const& mesh = light_meshes.at(chk_pos);
#if defined(LUX_GLES_2_0)
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glVertexAttribPointer(light_shader_attribs.pos,
            2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(LightMesh::Vert),
            (void*)offsetof(LightMesh::Vert, pos));
        glVertexAttribPointer(light_shader_attribs.col,
            3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(LightMesh::Vert),
            (void*)offsetof(LightMesh::Vert, col));
        glEnableVertexAttribArray(light_shader_attribs.pos);
        glEnableVertexAttribArray(light_shader_attribs.col);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
#elif defined(LUX_GL_3_3)
        glBindVertexArray(mesh.vao);
#endif
        //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDrawElements(GL_TRIANGLES, LightMesh::IDX_NUM,
            LightMesh::IDX_GL_TYPE, 0);
        //glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#if defined(LUX_GLES_2_0)
        glDisableVertexAttribArray(light_shader_attribs.pos);
        glDisableVertexAttribArray(light_shader_attribs.col);
#endif
    }
    glDisable(GL_BLEND);

    render_list.clear();
}

void map_reload_program() {
    LUX_LOG("reloading map program");
    glDeleteTextures(1, &tileset);
    glDeleteProgram(tile_program);
    glDeleteProgram(light_program);
    map_load_programs();
}

bool is_chunk_loaded(ChkPos const& pos) {
    return chunks.count(pos) > 0;
}

void load_chunk(ChkPos const& pos, NetSsSgnl::MapLoad::Chunk const& net_chunk) {
    LUX_LOG("loading chunk");
    LUX_LOG("    pos: {%zd, %zd, %zd}", pos.x, pos.y, pos.z);
    if(is_chunk_loaded(pos)) {
        LUX_LOG("chunk already loaded, ignoring it");
        return;
    }
    Chunk& chunk = chunks[pos];
    std::memcpy(chunk.voxels    , net_chunk.voxels,
                CHK_VOL * sizeof(VoxelId));
    std::memcpy(chunk.light_lvls, net_chunk.light_lvls,
                CHK_VOL * sizeof(LightLvl));

    MatMesh& mat_mesh = mat_meshes[pos];
    glGenBuffers(1, &mat_mesh.vbo);
    build_mat_mesh(mat_mesh, chunk);

    LightMesh& light_mesh = light_meshes[pos];
    glGenBuffers(1, &light_mesh.vbo);
    glGenBuffers(1, &light_mesh.ebo);
    build_light_mesh(light_mesh, chunk);
#if defined(LUX_GL_3_3)
    glGenVertexArrays(1, &mat_mesh.vao);
    glBindVertexArray(mat_mesh.vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry_mesh.ebo);

    glBindBuffer(GL_ARRAY_BUFFER, geometry_mesh.vbo);
    glVertexAttribPointer(tile_shader_attribs.pos,
        2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(GeometryMesh::Vert),
        (void*)offsetof(GeometryMesh::Vert, pos));
    glEnableVertexAttribArray(tile_shader_attribs.pos);

    glBindBuffer(GL_ARRAY_BUFFER, mat_mesh.vbo);
    glVertexAttribPointer(tile_shader_attribs.tex_pos,
        2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(MatMesh::Vert),
        (void*)offsetof(MatMesh::Vert, tex_pos));
    glEnableVertexAttribArray(tile_shader_attribs.tex_pos);

    glGenVertexArrays(1, &light_mesh.vao);
    glBindVertexArray(light_mesh.vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, light_mesh.ebo);

    glBindBuffer(GL_ARRAY_BUFFER, light_mesh.vbo);
    glVertexAttribPointer(light_shader_attribs.pos,
        2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(LightMesh::Vert),
        (void*)offsetof(LightMesh::Vert, pos));
    glVertexAttribPointer(light_shader_attribs.col,
        3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(LightMesh::Vert),
        (void*)offsetof(LightMesh::Vert, col));
    glEnableVertexAttribArray(light_shader_attribs.pos);
    glEnableVertexAttribArray(light_shader_attribs.col);
#endif
}

void light_update(ChkPos const& pos,
                  NetSsSgnl::LightUpdate::Chunk const& net_chunk) {
    if(!is_chunk_loaded(pos)) {
        LUX_LOG("chunk is not loaded");
        return;
    }
    Chunk& chunk = chunks.at(pos);
    std::memcpy(chunk.light_lvls, net_chunk.light_lvls,
                CHK_VOL * sizeof(LightLvl));
    build_light_mesh(light_meshes.at(pos), chunk);
}

Chunk const& get_chunk(ChkPos const& pos) {
    LUX_ASSERT(is_chunk_loaded(pos));
    return chunks.at(pos);
}

static void build_mat_mesh(MatMesh& mesh, Chunk const& chunk) {
    Arr<MatMesh::Vert, CHK_VOL * 4> verts;

    constexpr Vec2<U16> tex_quad[4] =
        {{0, 0}, {1, 0}, {0, 1}, {1, 1}};

    for(ChkIdx i = 0; i < CHK_VOL; ++i) {
        VoxelType const& vox_type = db_voxel_type(chunk.voxels[i]);
        for(U32 j = 0; j < 4; ++j) {
            verts[i * 4 + j].tex_pos = (Vec2<U16>)vox_type.tex_pos + tex_quad[j];
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(MatMesh::Vert) *
        CHK_VOL * 4, verts, GL_DYNAMIC_DRAW);
}

static void build_light_mesh(LightMesh& mesh, Chunk const& chunk) {
    Arr<LightMesh::Vert, CHK_VOL>           verts;
    Arr<LightMesh::Idx,  LightMesh::IDX_NUM> idxs;

    for(ChkIdx i = 0; i < CHK_VOL; ++i) {
        verts[i].pos = (Vec2<U16>)to_idx_pos(i);
        LightLvl light_lvl = chunk.light_lvls[i];
        verts[i].col = Vec3<U8>((light_lvl >> 12) & 0xF,
                                (light_lvl >>  8) & 0xF,
                                (light_lvl >>  4) & 0xF) * (U8)17u;
    }
    Uns idx_off = 0;
    for(ChkIdx i = 0; i < CHK_VOL; ++i) {
        if(i % CHK_SIZE != CHK_SIZE - 1 &&
           i / CHK_SIZE != CHK_SIZE - 1) {
            LightMesh::Idx constexpr idx_order[6] =
                {0, 1, CHK_SIZE, CHK_SIZE, CHK_SIZE + 1, 1};
            LightMesh::Idx constexpr flipped_idx_order[6] =
                {0, CHK_SIZE, CHK_SIZE + 1, CHK_SIZE + 1, 1, 0};
            if(glm::length((Vec3F)verts[i + 1].col) +
               glm::length((Vec3F)verts[i + CHK_SIZE].col) >
               glm::length((Vec3F)verts[i + 0].col) +
               glm::length((Vec3F)verts[i + CHK_SIZE + 1].col)) {
                for(Uns j = 0; j < 6; ++j) {
                    idxs[idx_off * 6 + j] = i + idx_order[j];
                }
            } else {
                for(Uns j = 0; j < 6; ++j) {
                    idxs[idx_off * 6 + j] = i + flipped_idx_order[j];
                }
            }
            ++idx_off;
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(LightMesh::Vert) *
        CHK_VOL, verts, GL_DYNAMIC_DRAW);

#if defined(LUX_GL_3_3)
    glBindVertexArray(mesh.vao);
#endif
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(LightMesh::Idx) *
        LightMesh::IDX_NUM, idxs, GL_DYNAMIC_DRAW);
}
