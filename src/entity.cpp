#include <config.hpp>
//
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/rotate_vector.hpp>
//
#include <db.hpp>
#include <rendering.hpp>
#include <client.hpp>
#include <ui.hpp>
#include "entity.hpp"

UiId ui_entity;
static EntityComps comps;
EntityComps& entity_comps = comps;
DynArr<EntityId> entities;

static GLuint program;
static GLuint tileset;

#pragma pack(push, 1)
struct Vert {
    Vec2F    pos;
    Vec2<U8> tex_pos;
};
#pragma pack(pop)

static gl::VertBuff    v_buff;
static gl::IdxBuff     i_buff;
static gl::VertContext context;
static gl::VertFmt     vert_fmt;

static void entity_render(U32, Transform const&);

void entity_init() {
    constexpr Vec2U tile_sz = {8, 8};
    char const* tileset_path = "entity_tileset.png";
    program = load_program("glsl/entity.vert", "glsl/entity.frag");

    glUseProgram(program);
    vert_fmt.init(program,
        {{"pos"    , 2, GL_FLOAT        , false, false},
         {"tex_pos", 2, GL_UNSIGNED_BYTE, false, false}});
    Vec2U tileset_sz;
    tileset = load_texture(tileset_path, tileset_sz);
    Vec2F tex_scale = (Vec2F)tile_sz / (Vec2F)tileset_sz;
    set_uniform("tex_scale", program, glUniform2fv, 1,
                glm::value_ptr(tex_scale));

    v_buff.init();
    i_buff.init();
    context.init({v_buff}, vert_fmt);
    ui_entity = ui_create(ui_camera, 50);
    ui_nodes[ui_entity].render = &entity_render;
}

static void entity_render(U32, Transform const& tr) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program);
    set_uniform("scale", program, glUniform2fv,
                1, glm::value_ptr(tr.scale));
    set_uniform("translation", program, glUniform2fv,
                1, glm::value_ptr(tr.pos));

    static DynArr<U32>  idxs;
    static DynArr<Vert> verts;
    idxs.clear();
    verts.clear();

    for(auto const& id : entities) {
        if(comps.pos.count(id) > 0 &&
           comps.visible.count(id) > 0) {
            Vec2F pos = Vec2F(comps.pos.at(id));
            Vec2F quad_sz = comps.visible.at(id).quad_sz;
            Vec2F origin = {0.f, 0.f};
            F32   angle = 0.f;
            F32   origin_angle = 0.f;
            if(comps.orientation.count(id) > 0) {
                angle  = comps.orientation.at(id).angle;
                origin = comps.orientation.at(id).origin;
            }
            EntityId p_id = id;
            while(comps.parent.count(p_id) > 0) {
                p_id = comps.parent.at(p_id);
                F32   parent_angle = 0.f;
                Vec2F parent_origin = {0.f, 0.f};
                if(comps.orientation.count(p_id) > 0) {
                    parent_angle = comps.orientation.at(p_id).angle;
                    parent_origin = comps.orientation.at(p_id).origin;
                    angle += parent_angle;
                    origin_angle += parent_angle;
                    pos = glm::rotate(pos, parent_angle);
                    pos += parent_origin;
                    pos -= glm::rotate(parent_origin, parent_angle);
                }
                if(comps.pos.count(p_id) > 0) {
                    pos += comps.pos.at(p_id);
                }
            }
            Vec2F rot_shift = glm::rotate(origin, angle);
            add_dbg_arrow({pos, pos + rot_shift}, {0.f, 0.f, 1.f, 1.f});

            for(auto const& idx : quad_idxs<U32>) {
                idxs.emplace(verts.len + idx);
            }
            EntitySprite const& sprite =
                db_entity_sprite(comps.visible.at(id).visible_id);
            for(Uns i = 0; i < 4; ++i) {
                verts.emplace(Vert
                    {glm::rotate((quad_sz * quad<F32>[i]) - origin, angle) +
                    glm::rotate(origin, origin_angle) + pos,
                     u_quad<U8>[i] * sprite.sz + sprite.pos});
            }
            if(comps.text.count(id) > 0 && comps.name.count(id) == 0) {
                ui_erase(comps.text.at(id).text);
                comps.text.erase(id);
            }
            //@TODO we need to remove when entity is removed
            if(comps.name.count(id) > 0) {
                auto const& name = comps.name.at(id);
                if(comps.text.count(id) == 0) {
                    comps.text[id] = {
                        ui_text_create(ui_entity, {{0, 0}, {1.f, 1.f}},
                                       name)};
                }
                UiText* text = ui_texts.at(comps.text.at(id).text);
                if(text == nullptr) {
                    comps.text.erase(id);
                } else {
                    //@TODO what if UI was deleted as well?
                    ui_nodes[text->ui].tr.pos =
                        pos - Vec2F(name.len / 2.f, 2.f);
                }
            }
        }
    }

    context.bind();
    v_buff.bind();
    v_buff.write(verts.len, verts.beg, GL_DYNAMIC_DRAW);
    i_buff.bind();
    i_buff.write(idxs.len, idxs.beg, GL_DYNAMIC_DRAW);

    glBindTexture(GL_TEXTURE_2D, tileset);
    glDrawElements(GL_TRIANGLES, idxs.len, GL_UNSIGNED_INT, 0);
    context.unbind();
    glDisable(GL_BLEND);
}

void set_net_entity_comps(NetSsTick::EntityComps const& net_comps) {
    comps.pos.clear();
    comps.name.clear();
    comps.visible.clear();
    comps.orientation.clear();
    for(auto const& pos : net_comps.pos) {
        comps.pos[pos.first] = pos.second;
    }
    for(auto const& name : net_comps.name) {
        comps.name[name.first] = name.second;
    }
    for(auto const& visible : net_comps.visible) {
        comps.visible[visible.first] =
            {visible.second.visible_id, visible.second.quad_sz};
    }
    for(auto const& container : net_comps.container) {
        comps.container[container.first].items = container.second.items;
    }
    for(auto const& orientation : net_comps.orientation) {
        comps.orientation[orientation.first] =
            {orientation.second.origin, orientation.second.angle};
    }
    for(auto const& parent : net_comps.parent) {
        comps.parent[parent.first] = parent.second;
    }
}
