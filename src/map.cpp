#include <cstring>
//
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/fast_square_root.hpp>
//
#include <lux_shared/common.hpp>
#include <lux_shared/map.hpp>
#include <lux_shared/util/packer.hpp>
//
#include <imgui/imgui.h>
#include <rendering.hpp>
#include <db.hpp>
#include <client.hpp>
#include <ui.hpp>
#include "map.hpp"

static UiId        ui_map;
static gl::VertFmt vert_fmt;
static GLuint      program;
static GLuint      tileset;

struct Mesh {
#pragma pack(push, 1)
    struct Vert {
        Vec3<U8> pos;
        U8       norm;
        U8       tex;
    };
#pragma pack(pop)

    gl::VertBuff v_buff;
    gl::IdxBuff i_buff;
    gl::VertContext context;
    DynArr<Vert> verts;
    DynArr<U16>  idxs;
    bool is_allocated = false;

    void alloc() {
        LUX_ASSERT(not is_allocated);
        v_buff.init();
        i_buff.init();
        context.init({v_buff}, vert_fmt);
        is_allocated = true;
    }

    void dealloc() {
        LUX_ASSERT(is_allocated);
        context.deinit();
        i_buff.deinit();
        v_buff.deinit();
        is_allocated = false;
    }

    void operator=(Mesh&& that) {
        v_buff  = move(that.v_buff);
        i_buff  = move(that.i_buff);
        context = move(that.context);
        verts   = move(that.verts);
        idxs    = move(that.idxs);
        is_allocated = move(that.is_allocated);
        that.is_allocated = false;
    }
    Mesh() = default;
    Mesh(Mesh&& that) { *this = move(that); }
};

struct Renderer {
#pragma pack(push, 1)
    struct Vert {
        Vec2F pos;
        Vec2F tex_pos;
    };
#pragma pack(pop)

    gl::IdxBuff     i_buff;
    gl::VertBuff    v_buff;
    gl::VertFmt     vert_fmt;
    gl::VertContext context;

    GLuint g_buff;
    GLuint g_pos;
    GLuint g_norm;
    GLuint g_col;
    GLuint rbo;

    GLuint program;
} static renderer;

static Mesh debug_mesh_0;
static Mesh debug_mesh_1;

static DynArr<Mesh>   meshes;
VecSet<ChkPos>        chunk_requests;

static ChkCoord render_dist = 2;
static ChkPos   last_player_chk_pos = to_chk_pos(glm::floor(last_player_pos));

static void map_io_tick(  U32, Transform const&, IoContext&);

void map_init() {
    char const* tileset_path = "tileset.png";
    Vec2U const block_size = {1, 1};
    program  = load_program("glsl/block.vert" , "glsl/block.frag");
    Vec2U tileset_size;
    tileset = load_texture(tileset_path, tileset_size);
    Vec2F tex_scale = (Vec2F)block_size / (Vec2F)tileset_size;

    glUseProgram(program);
    set_uniform("tex_scale", program, glUniform2fv,
                1, glm::value_ptr(tex_scale));
    vert_fmt.init(
        {{3, GL_UNSIGNED_BYTE, false, false},
         {1, GL_UNSIGNED_BYTE, false, false},   //@TODO this should be unsigned
         {1, GL_UNSIGNED_BYTE, false, false}}); //~this as well

    ui_map = ui_create(ui_camera);
    ui_nodes[ui_map].io_tick = &map_io_tick;

    gl::VertContext::unbind_all();
    debug_mesh_0.alloc();

    debug_mesh_0.verts.resize(4);
    debug_mesh_0.idxs.resize(6);
    for(Uns i = 0; i < 6; ++i) {
        debug_mesh_0.idxs[i] = quad_idxs<U16>[i];
    }
    for(Uns i = 0; i < 4; ++i) {
        debug_mesh_0.verts[i].pos  = Vec3<U8>(u_quad<U32>[i] * (U32)CHK_SIZE, 0);
        debug_mesh_0.verts[i].norm = 0b101;
        debug_mesh_0.verts[i].tex  = 0;
    }

    gl::VertContext::unbind_all();
    debug_mesh_0.i_buff.bind();
    debug_mesh_0.i_buff.write(6, debug_mesh_0.idxs.beg, GL_STATIC_DRAW);
    debug_mesh_0.v_buff.bind();
    debug_mesh_0.v_buff.write(4, debug_mesh_0.verts.beg, GL_STATIC_DRAW);

    glGenFramebuffers(1, &renderer.g_buff);
    glBindFramebuffer(GL_FRAMEBUFFER, renderer.g_buff);

    glGenTextures(1, &renderer.g_pos);
    glBindTexture(GL_TEXTURE_2D, renderer.g_pos);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 1600, 900, //@TODO screen sz
                 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           renderer.g_pos, 0);

    glGenTextures(1, &renderer.g_norm);
    glBindTexture(GL_TEXTURE_2D, renderer.g_norm);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 1600, 900, //@TODO screen sz
                 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                           renderer.g_norm, 0);

    glGenTextures(1, &renderer.g_col);
    glBindTexture(GL_TEXTURE_2D, renderer.g_col);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 1600, 900, //@TODO screen sz
                 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D,
                           renderer.g_col, 0);

    glGenRenderbuffers(1, &renderer.rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, renderer.rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, 1600, 900); //@TODO screen sz
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, renderer.rbo);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LUX_FATAL("failed to create framebuffer for map deferred shading");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    Arr<Renderer::Vert, 4> verts;
    for(Uns i = 0; i < 4; ++i) {
        verts[i].pos     = quad<F32>[i];
        verts[i].tex_pos = u_quad<F32>[i];
    }
    renderer.v_buff.init();
    renderer.v_buff.bind();
    renderer.v_buff.write(4, verts, GL_STATIC_DRAW);

    Arr<U32, 6> idxs;
    for(Uns i = 0; i < 6; ++i) {
        idxs[i] = quad_idxs<F32>[5 - i];
    }
    renderer.i_buff.init();
    renderer.i_buff.bind();
    renderer.i_buff.write(6, idxs, GL_STATIC_DRAW);

    renderer.program = load_program("glsl/map_deferred.vert",
                                    "glsl/map_deferred.frag");
    renderer.vert_fmt.init(
        {{2, GL_FLOAT, false, false},
         {2, GL_FLOAT, false, false}});
    renderer.context.init({renderer.v_buff}, renderer.vert_fmt);
}

void map_deinit() {
    //@TODO destroy more stuff from renderer?
    renderer.context.deinit();
    renderer.i_buff.deinit();
    renderer.v_buff.deinit();

    for(auto& mesh : meshes) {
        if(mesh.is_allocated) {
            mesh.dealloc();
        }
    }
    meshes.dealloc_all();
    debug_mesh_0.dealloc();
}

static void map_io_tick(U32, Transform const&, IoContext& context) {
    for(Uns i = 0; i < context.mouse_events.len; ++i) {
        //@TODO erase event
        auto const& event = context.mouse_events[i];
        if(event.button == GLFW_MOUSE_BUTTON_LEFT &&
           event.action == GLFW_PRESS) {
            LUX_UNIMPLEMENTED();
        } else if(event.button == GLFW_MOUSE_BUTTON_RIGHT &&
           event.action == GLFW_PRESS) {
            LUX_UNIMPLEMENTED();
        }
    }
    F32 render_dist_change = 0.f;
    for(Uns i = 0; i < context.scroll_events.len; ++i) {
        //@TODO erase event
        auto const& event = context.scroll_events[i];
        render_dist_change += event.off;
    }
    render_dist = round(max((F32)render_dist + render_dist_change, 0.f));
    //@TODO glfwGetKey?
    Vec3F dir(0.f);
    if(glfwGetKey(context.win, GLFW_KEY_W)) {
        dir.x += 1.f;
    }
    if(glfwGetKey(context.win, GLFW_KEY_S)) {
        dir.x -= 1.f;
    }
    if(glfwGetKey(context.win, GLFW_KEY_D)) {
        dir.y += 1.f;
    }
    if(glfwGetKey(context.win, GLFW_KEY_A)) {
        dir.y -= 1.f;
    }
    F32 len = length(dir);
    if(len != 0.f) {
        dir /= len;
        cs_tick.is_moving = true;
        cs_tick.move_dir = dir;
    } else {
        cs_tick.is_moving = false;
    }

    //@TODO
    static Vec2F last_mouse_pos = {0, 0};
    static Vec2F prot = {0, 0};
    Vec2F diff = (last_mouse_pos - context.mouse_pos) * 0.005f;
    last_mouse_pos = context.mouse_pos;
    prot += diff;
    prot.x = s_mod_clamp(prot.x, tau / 2.f);
    prot.y = clamp(prot.y, -tau / 4.f + .001f, tau / 4.f - .001f);
    Vec2F s_prot = {s_mod_clamp((prot.x + tau / 4.f) / (-tau / 2.f), 1.f),
                    prot.y / (tau / 2.f)};
    cs_tick.yaw_pitch = s_prot;

    EntityVec camera_pos = -ui_nodes[ui_camera].tr.pos;
    camera_pos.z += 1.4f; //@TODO camera height

    ChkCoord mesh_load_size = 2 * render_dist + 1;
    ChkPos chk_pos = to_chk_pos(glm::floor(camera_pos));
    static ChkCoord last_render_dist = 0;
    ChkCoord last_mesh_load_size = 2 * last_render_dist + 1;

    ChkPos chk_diff           = chk_pos - last_player_chk_pos;
    ChkCoord render_dist_diff = render_dist - last_render_dist;
    chk_diff -= render_dist_diff;
    if(chk_diff != ChkPos(0)) {
        //@XXX downsizing render distance crashed in the past
        SizeT meshes_num = glm::pow(mesh_load_size, 3);
        auto get_idx = [&](ChkPos pos, ChkCoord size) {
            return pos.x +
                   pos.y * size +
                   pos.z * size * size;
        };
        //@TODO send unload signal to server
        static DynArr<Mesh> new_meshes;
        for(auto& mesh : new_meshes) { //@TODO temp workaround
            if(mesh.is_allocated) {
                mesh.dealloc();
            }
        }
        new_meshes.resize(meshes_num);
        for(ChkCoord z =  chk_diff.z > 0 ? chk_diff.z : 0;
                     z < (chk_diff.z < 0 ? chk_diff.z : 0) + last_mesh_load_size;
                   ++z) {
            for(ChkCoord y =  chk_diff.y > 0 ? chk_diff.y : 0;
                         y < (chk_diff.y < 0 ? chk_diff.y : 0) + last_mesh_load_size;
                       ++y) {
                for(ChkCoord x =  chk_diff.x > 0 ? chk_diff.x : 0;
                             x < (chk_diff.x < 0 ? chk_diff.x : 0) + last_mesh_load_size;
                           ++x) {
                    ChkPos src(x, y, z);
                    SizeT src_idx = get_idx(src, last_mesh_load_size);
                    ChkPos dst = src - chk_diff;
                    SizeT dst_idx = get_idx(dst, mesh_load_size);
                    if(src_idx >= meshes.len ||
                       not meshes[src_idx].is_allocated) {
                        if(dst_idx < new_meshes.len &&
                           new_meshes[dst_idx].is_allocated) {
                            new_meshes[dst_idx].dealloc();
                        }
                        continue;
                    }
                    if(dst_idx >= new_meshes.len) {
                        if(meshes[src_idx].is_allocated) {
                            meshes[src_idx].dealloc();
                        }
                        continue;
                    }
                    new_meshes[dst_idx] = move(meshes[src_idx]);
                }
            }
        }
        swap(meshes, new_meshes);
    }

    last_player_chk_pos = chk_pos;
    last_render_dist    = render_dist;

    glm::mat4 mvp =
        {1, 0, 0, 0,
         0, 0, 1, 0,
         0, 1, 0, 0,
         0, 0, 0, 1};
    Vec3F direction;
    direction.x = glm::cos(prot.y) * glm::cos(-prot.x);
    direction.y = glm::sin(prot.y);
    direction.z = glm::cos(prot.y) * glm::sin(-prot.x);
    direction = glm::normalize(direction);

    F32 temp = camera_pos.z;
    camera_pos.z = camera_pos.y;
    camera_pos.y = temp;
    F32 z_far = (F32)render_dist * (F32)CHK_SIZE;

    int win_w, win_h;
    //@TODO we probably should use something more universal, like our local
    //ui size
    glfwGetWindowSize(context.win, &win_w, &win_h);
    F32 aspect_ratio = (F32)win_w / (F32)win_h;
    mvp = glm::perspective(glm::radians(70.f), aspect_ratio, 0.1f, z_far) *
          glm::lookAt(camera_pos, camera_pos + direction, Vec3F(0, 1, 0)) * mvp;

    struct DrawData {
        U32 mesh_idx;
        Vec3F pos;
    };
    static DynArr<DrawData> draw_queue;
    draw_queue.clear();
    struct {
        U64 chunks_num = 0;
        U64 real_chunks_num = 0;
        U64 trigs_num  = 0;
    } status;
    for(Uns i = 0; i < meshes.len; ++i) {
        Mesh* mesh = &meshes[i];
        if(mesh->is_allocated && mesh->verts.len <= 0) continue;
        ChkPos pos = { i % mesh_load_size,
                      (i / mesh_load_size) % mesh_load_size,
                       i / (mesh_load_size * mesh_load_size)};
        pos -= render_dist;
        pos += chk_pos;
        if(glm::distance((Vec3F)chk_pos, (Vec3F)pos) > render_dist) {
            continue;
        }
        Vec3F center = (Vec3F)pos * (Vec3F)CHK_SIZE + (Vec3F)(CHK_SIZE / 2);
        Vec4F c_center = mvp * Vec4F(center, 1.f);
        c_center.x /= c_center.w;
        c_center.y /= c_center.w;
        F32 constexpr chk_rad = CHK_SIZE * glm::sqrt(3);
        if(c_center.z < -chk_rad) continue;
        F32 c_chk_rad = chk_rad / glm::abs(c_center.w);
        if(glm::abs(c_center.x) > 1.f + c_chk_rad ||
           glm::abs(c_center.y) > 1.f + c_chk_rad) continue;

        status.chunks_num++;
        draw_queue.push({i, pos});
    }
    {
        Vec3F f_pos = chk_pos;
        std::sort(draw_queue.begin(), draw_queue.end(),
            [&](DrawData const& a, DrawData const& b) {
                return glm::fastDistance(a.pos, f_pos) <
                       glm::fastDistance(b.pos, f_pos);
            });
    }
    static bool wireframe = false;
    if(wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }
    Vec3F ambient_light = Vec3F(0.f);
        /*glm::mix(Vec3F(0.01f, 0.015f, 0.02f), Vec3F(1.f, 1.f, 0.9f),
                 (ss_tick.day_cycle + 1.f) / 2.f);*/

    //glClearColor(ambient_light.r, ambient_light.g, ambient_light.b, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //@TODO remove one of clear color
    glBindFramebuffer(GL_FRAMEBUFFER, renderer.g_buff);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glCullFace(GL_FRONT);
    glEnable(GL_CULL_FACE);
    glUseProgram(program);
    set_uniform("tileset" , program, glUniform1i, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tileset);
    set_uniform("time", program, glUniform1f, glfwGetTime());
    set_uniform("ambient_light", program, glUniform3fv,
                1, glm::value_ptr(ambient_light));
    set_uniform("mvp", program, glUniformMatrix4fv,
                1, GL_FALSE, glm::value_ptr(mvp));
    constexpr GLenum buffers[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, buffers);

    for(auto const& draw_data : draw_queue) {
        Mesh* mesh = &meshes[draw_data.mesh_idx];
        auto const& pos = draw_data.pos;
        if(not mesh->is_allocated) {
            //@NOTE we request the chunk here, because we want to request them
            //in a priority sorted by distance to player, i.e. closer chunks
            //are requested earlier
            chunk_requests.emplace(pos);
            continue;
        }
        if(mesh != &debug_mesh_0 && mesh != &debug_mesh_1) {
            status.real_chunks_num++;
        }
        status.trigs_num += mesh->idxs.len / 3;
        Vec3F chk_translation = pos * (F32)CHK_SIZE;

        set_uniform("chk_pos", program, glUniform3fv, 1,
            glm::value_ptr(chk_translation));
        //@TODO multi draw
        mesh->context.bind();
        mesh->i_buff.bind();
        glDrawElements(GL_TRIANGLES, mesh->idxs.len, GL_UNSIGNED_SHORT, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glDisable(GL_DEPTH_TEST);
    if(wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(renderer.program);

    set_uniform("g_pos" , renderer.program, glUniform1i, 0);
    set_uniform("g_norm", renderer.program, glUniform1i, 1);
    set_uniform("g_col" , renderer.program, glUniform1i, 2);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer.g_pos);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, renderer.g_norm);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, renderer.g_col);

    renderer.context.bind();
    renderer.i_buff.bind();
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    //@TODO is this needed?
    glBindFramebuffer(GL_READ_FRAMEBUFFER, renderer.g_buff);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, 1600, 900, 0, 0, 1600, 900, GL_DEPTH_BUFFER_BIT,
                      GL_NEAREST); //@TODO screen sz
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    static F64 avg = 0.0;
    static U32 denom = 0;
    static F64 timer = glfwGetTime();
    static F64 delta_max = 0;
    F64 now = glfwGetTime();
    F64 delta = now - timer;
    if(delta > delta_max) delta_max = delta;
    timer = now;

    SizeT constexpr plot_sz = 512;
    static F32 last_delta[plot_sz] = {0.0};
    for(Uns i = 0; i < plot_sz - 1; ++i) {
        last_delta[i] = last_delta[i + 1];
    }
    last_delta[plot_sz - 1] = (F32)(delta * 1000.0);

    avg += delta;
    denom++;
    static F64 last_avg = 0.0;
    if(denom >= 8) {
        last_avg = avg / (F64)denom;
        avg = 0.0;
        denom = 0;
    }
    F64 fps = 1.0 / last_avg;

    ImGui::Begin("render status");
    ImGui::Text("delta: %.2F", last_avg * 1000.0);
    ImGui::Text("delta_max: %.2F", delta_max * 1000.0);
    ImGui::PlotHistogram("", last_delta, plot_sz, 0,
                         nullptr, 0.f, FLT_MAX, {200, 80});
    if(ImGui::Button("wireframe mode")) {
        wireframe = !wireframe;
    }
    ImGui::Text("fps: %d", (int)fps);
    ImGui::Text("render dist: %zu", (Uns)render_dist);
    ImGui::Text("pending chunks num: %zu", chunk_requests.size());
    ImGui::Text("chunks num: %zu", status.chunks_num);
    ImGui::Text("real chunks num: %zu", status.real_chunks_num);
    ImGui::Text("trigs num: %zu", status.trigs_num);
    ImGui::End();
}

///returns -1 if out of bounds
static Int get_fov_idx(ChkPos const& pos) {
    ChkPos idx_pos = (pos - last_player_chk_pos) + render_dist;
    ChkCoord size = render_dist * 2 + 1;
    if(clamp(idx_pos, ChkPos(0), ChkPos(size)) != idx_pos) return -1;
    Int idx = idx_pos.x + idx_pos.y * size + idx_pos.z * size * size;
    return idx;
}

void map_load_chunks(NetSsSgnl::ChunkLoad const& net_chunks) {
    gl::VertContext::unbind_all();
    for(auto const& pair : net_chunks.chunks) {
        ChkPos chk_pos = pair.first;
        if(chunk_requests.count(chk_pos) > 0) {
            chunk_requests.erase(chk_pos);
        }
        auto const& net_chunk = pair.second;
        Int idx = get_fov_idx(chk_pos);
        if(idx < 0) {
            LUX_LOG_WARN("received chunk {%zd, %zd, %zd} out of load range",
                chk_pos.x, chk_pos.y, chk_pos.z);
            continue;
        }
        if(meshes[idx].is_allocated) {
            LUX_LOG_WARN("received chunk {%zd, %zd, %zd} alread loaded",
                chk_pos.x, chk_pos.y, chk_pos.z);
            continue;
        }
        Mesh& mesh = meshes[idx];
        SizeT faces_num = 0;
        faces_num += net_chunk.faces.len;
        mesh.verts.resize(faces_num * 4);
        mesh.idxs.resize(faces_num * 6);
        Arr<Vec3<U8>, 3 * 4> vert_offs = {
            {0, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 1, 1},
            {0, 0, 0}, {0, 0, 1}, {1, 0, 0}, {1, 0, 1},
            {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0},
        };
        for(Uns i = 0; i < net_chunk.faces.len; ++i) {
            auto const& n_face = net_chunk.faces[i];
            ChkIdx idx = n_face.idx;
            U8 axis = (n_face.orientation & 0b110) >> 1;
            LUX_ASSERT(axis != 0b11);
            U8 sign = (n_face.orientation & 1);
            Vec3<U8> face_off(0);
            face_off[axis] = 1;
            if(sign) {
                for(Uns j = 0; j < 6; ++j) {
                    mesh.idxs[i * 6 + j] = quad_idxs<U16>[j] + i * 4;
                }
            } else {
                for(Uns j = 0; j < 6; ++j) {
                    mesh.idxs[i * 6 + j] = quad_idxs<U16>[5 - j] + i * 4;
                }
            }
            for(Uns j = 0; j < 4; ++j) {
                auto& vert = mesh.verts[i * 4 + j];
                vert.pos  = (Vec3<U8>)to_idx_pos(idx) + face_off +
                    vert_offs[axis * 4 + j];
                vert.norm = n_face.orientation;
                vert.tex  = n_face.id;
            }
        }
        mesh.alloc();
        mesh.v_buff.bind();
        mesh.v_buff.write(mesh.verts.len, mesh.verts.beg, GL_DYNAMIC_DRAW);
        mesh.i_buff.bind();
        mesh.i_buff.write(mesh.idxs.len, mesh.idxs.beg, GL_DYNAMIC_DRAW);
    }
}

void map_update_chunks(NetSsSgnl::ChunkUpdate const& net_chunks) {
    gl::VertContext::unbind_all();
    for(auto const& pair : net_chunks.chunks) {
        ChkPos pos = pair.first;
        auto const& net_chunk = pair.second;
        LUX_LOG("updating chunk {%zd, %zd, %zd}", pos.x, pos.y, pos.z);
        Int idx = get_fov_idx(pos);
        if(idx < 0 || not meshes[idx].is_allocated) {
            LUX_LOG_WARN("received chunk update for unloaded chunk"
                " {%zd, %zd, %zd}", pos.x, pos.y, pos.z);
            continue;
        }
        Mesh& mesh = meshes[idx];
        for(auto const& removed_face : net_chunk.removed_faces) {
            for(Uns i = (removed_face + 1) * 6; i < mesh.idxs.len; ++i) {
                mesh.idxs[i] -= 4;
            }
            //@XXX this crashed in the past
            mesh.idxs.erase(removed_face * 6, 6);
            mesh.verts.erase(removed_face * 4, 4);
        }
        Arr<Vec3<U8>, 3 * 4> vert_offs = {
            {0, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 1, 1},
            {0, 0, 0}, {0, 0, 1}, {1, 0, 0}, {1, 0, 1},
            {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0},
        };
        Uns off = mesh.verts.len / 4;
        mesh.idxs.resize(mesh.idxs.len + net_chunk.added_faces.len * 6);
        mesh.verts.resize(mesh.verts.len + net_chunk.added_faces.len * 4);
        for(Uns i = 0; i < net_chunk.added_faces.len; ++i) {
            auto const& n_face = net_chunk.added_faces[i];
            ChkIdx idx = n_face.idx;
            U8 axis = (n_face.orientation & 0b110) >> 1;
            LUX_ASSERT(axis != 0b11);
            U8 sign = (n_face.orientation & 1);
            Vec3<U8> face_off(0);
            face_off[axis] = 1;
            if(sign) {
                for(Uns j = 0; j < 6; ++j) {
                    mesh.idxs[off * 6 + j] = quad_idxs<U16>[j] + off * 4;
                }
            } else {
                for(Uns j = 0; j < 6; ++j) {
                    mesh.idxs[off * 6 + j] = quad_idxs<U16>[5 - j] + off * 4;
                }
            }
            for(Uns j = 0; j < 4; ++j) {
                auto& vert = mesh.verts[off * 4 + j];
                vert.pos  = (Vec3<U8>)to_idx_pos(idx) + face_off +
                    vert_offs[axis * 4 + j];
                vert.norm = n_face.orientation;
                vert.tex  = n_face.id;
            }
            ++off;
        }
        mesh.i_buff.bind();
        mesh.i_buff.write(mesh.idxs.len, mesh.idxs.beg, GL_DYNAMIC_DRAW);
        mesh.v_buff.bind();
        mesh.v_buff.write(mesh.verts.len, mesh.verts.beg, GL_DYNAMIC_DRAW);
    }
}

