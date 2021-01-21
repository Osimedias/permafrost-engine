/*
 *  This file is part of Permafrost Engine. 
 *  Copyright (C) 2020 Eduard Permyakov 
 *
 *  Permafrost Engine is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Permafrost Engine is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 *  Linking this software statically or dynamically with other modules is making 
 *  a combined work based on this software. Thus, the terms and conditions of 
 *  the GNU General Public License cover the whole combination. 
 *  
 *  As a special exception, the copyright holders of Permafrost Engine give 
 *  you permission to link Permafrost Engine with independent modules to produce 
 *  an executable, regardless of the license terms of these independent 
 *  modules, and to copy and distribute the resulting executable under 
 *  terms of your choice, provided that you also meet, for each linked 
 *  independent module, the terms and conditions of the license of that 
 *  module. An independent module is a module which is not derived from 
 *  or based on Permafrost Engine. If you modify Permafrost Engine, you may 
 *  extend this exception to your version of Permafrost Engine, but you are not 
 *  obliged to do so. If you do not wish to do so, delete this exception 
 *  statement from your version.
 *
 */

#include "region.h"
#include "game_private.h"
#include "position.h"
#include "../ui.h"
#include "../main.h"
#include "../camera.h"
#include "../event.h"
#include "../collision.h"
#include "../render/public/render.h"
#include "../render/public/render_ctrl.h"
#include "../lib/public/pf_string.h"
#include "../lib/public/khash.h"
#include "../lib/public/vec.h"

#include <assert.h>
#include <math.h>


#define MAX(a, b)    ((a) > (b) ? (a) : (b))
#define ARR_SIZE(a)  (sizeof(a)/sizeof(a[0]))
#define EPSILON      (1.0f/1024)

VEC_TYPE(str, const char*)
VEC_IMPL(static inline, str, const char*)

VEC_TYPE(uid, uint32_t)
VEC_IMPL(static inline, uid, uint32_t)

struct region{
    enum region_type type;
    union{
        float radius;
        struct{
            float xlen; 
            float zlen;
        };
    };
    vec2_t pos;
    vec_uid_t curr_ents;
    vec_uid_t prev_ents;
};

enum op{
    ADD, REMOVE
};

KHASH_MAP_INIT_STR(region, struct region)
KHASH_SET_INIT_STR(name)

/*****************************************************************************/
/* STATIC VARIABLES                                                          */
/*****************************************************************************/

static const struct map *s_map;
static khash_t(region)  *s_regions;
static bool              s_render = false;
/* Keep track of which regions intersect every chunk, 
 * making a poor man's 2-level tree */
static vec_str_t        *s_intersecting;
static khash_t(name)    *s_dirty;
/* Keep the event argument strings around for one tick, so that 
 * they can be used by the event handlers safely */
static vec_str_t         s_eventargs;

/*****************************************************************************/
/* STATIC FUNCTIONS                                                          */
/*****************************************************************************/

static bool region_intersects_chunk(const struct region *reg, struct map_resolution res, struct tile_desc td)
{
    struct box chunk = M_Tile_ChunkBounds(res, M_GetPos(s_map), td.chunk_r, td.chunk_c);

    switch(reg->type) {
    case REGION_CIRCLE: {
        return C_CircleRectIntersection(reg->pos, reg->radius, chunk);
    }
    case REGION_RECTANGLE: {
        struct box bounds = (struct box) {
            reg->pos.x + reg->xlen/2.0,
            reg->pos.z - reg->zlen/2.0,
            reg->xlen,
            reg->zlen
        };
        return C_RectRectIntersection(bounds, chunk);
    }
    default: return (assert(0), false);
    }
}

static bool compare_keys(const char **a, const char **b)
{
    return *a == *b;
}

static bool compare_uids(uint32_t *a, uint32_t *b)
{
    return *a == *b;
}

static int compare_uint32s(const void* a, const void* b)
{
    uint32_t uida = *(uint32_t*)a;
    uint32_t uidb = *(uint32_t*)b;
    return (uida - uidb);
}

static void region_update_intersecting(const char *name, const struct region *reg, int op)
{
    struct map_resolution res;
    M_GetResolution(s_map, &res);

    int delta;
    int chunklen = MAX(X_COORDS_PER_TILE * res.chunk_w * res.tile_w, Z_COORDS_PER_TILE * res.chunk_h * res.tile_h);

    switch(reg->type) {
    case REGION_CIRCLE:
        delta = ceil(reg->radius / chunklen);
        break;
    case REGION_RECTANGLE:
        delta = MAX(ceil(reg->xlen/2.0f / chunklen), ceil(reg->zlen/2.0f / chunklen));
        break;
    default: assert(0);
    }

    struct tile_desc td;
    if(!M_Tile_DescForPoint2D(res, M_GetPos(s_map), reg->pos, &td))
        return;

    for(int dr = -delta; dr <= delta; dr++) {
    for(int dc = -delta; dc <= delta; dc++) {

        struct tile_desc curr = td;
        if(!M_Tile_RelativeDesc(res, &curr, dc * res.tile_h, dr * res.tile_w))
            continue;

        if(!region_intersects_chunk(reg, res, curr))
            continue;

        vec_str_t *chunk = &s_intersecting[curr.chunk_r * res.chunk_w + curr.chunk_c];

        switch(op) {
        case REMOVE: {
            int idx = vec_str_indexof(chunk, name, compare_keys);
            if(idx != -1) {
                vec_str_del(chunk, idx);
            }
            break;
        }
        case ADD: {
            vec_str_push(chunk, name);
            break;
        }
        default: assert(0);
        }
    }}
}

static bool region_add(const char *name, struct region reg)
{
    if(kh_get(region, s_regions, name) != kh_end(s_regions))
        return false;

    const char *key = pf_strdup(name);
    if(!key)
        return false;

    int status;
    khiter_t k = kh_put(region, s_regions, key, &status);
    if(status == -1)
        return false;

    kh_value(s_regions, k) = reg;
    region_update_intersecting(key, &reg, ADD);
    return true;
}

static bool region_contains(const struct region *reg, vec2_t point)
{
    switch(reg->type) {
    case REGION_CIRCLE: {
        return C_PointInsideCircle2D(point, reg->pos, reg->radius);
    }
    case REGION_RECTANGLE: {
        vec2_t corners[4] = {
            (vec2_t){reg->pos.x + reg->xlen/2.0f, reg->pos.z - reg->zlen/2.0f},
            (vec2_t){reg->pos.x - reg->xlen/2.0f, reg->pos.z - reg->zlen/2.0f},
            (vec2_t){reg->pos.x - reg->xlen/2.0f, reg->pos.z + reg->zlen/2.0f},
            (vec2_t){reg->pos.x + reg->xlen/2.0f, reg->pos.z + reg->zlen/2.0f},
        };
        return C_PointInsideRect2D(point, corners[0], corners[2], corners[1], corners[3]);
    }
    default: 
        return (assert(0), false);
    }
}

static size_t regions_at_point(vec2_t point, size_t maxout, struct region *out[static maxout], 
                               const char *out_names[static maxout])
{
    struct map_resolution res;
    M_GetResolution(s_map, &res);

    struct tile_desc td;
    if(!M_Tile_DescForPoint2D(res, M_GetPos(s_map), point, &td))
        return 0;

    size_t ret = 0;
    vec_str_t *chunk = &s_intersecting[td.chunk_r * res.chunk_w + td.chunk_c];
    for(int i = 0; i < vec_size(chunk); i++) {

        if(ret == maxout)
            break;

        const char *name = vec_AT(chunk, i);
        khiter_t k = kh_get(region, s_regions, name);
        assert(k != kh_end(s_regions));

        struct region *reg = &kh_value(s_regions, k);
        if(!region_contains(reg, point))
            continue;

        out[ret] = reg;
        out_names[ret] = name;
        ret++;
    }
    return ret;
}

static void regions_remove_ent(uint32_t uid, vec2_t pos)
{
    const char *names[512];
    struct region *regs[512];
    size_t nregs = regions_at_point(pos, ARR_SIZE(regs), regs, names);

    for(int i = 0; i < nregs; i++) {
        int idx = vec_uid_indexof(&regs[i]->curr_ents, uid, compare_uids);
        if(idx == -1)
            continue;
        vec_uid_del(&regs[i]->curr_ents, idx);
        kh_put(name, s_dirty, names[i], &(int){0});
    }
}

static void regions_add_ent(uint32_t uid, vec2_t pos)
{
    const struct entity *ent = G_EntityForUID(uid);
    if(!ent || (ent->flags & (ENTITY_FLAG_ZOMBIE | ENTITY_FLAG_MARKER)))
        return;

    const char *names[512];
    struct region *regs[512];
    size_t nregs = regions_at_point(pos, ARR_SIZE(regs), regs, names);

    for(int i = 0; i < nregs; i++) {

        int idx = vec_uid_indexof(&regs[i]->curr_ents, uid, compare_uids);
        if(idx != -1)
            continue;

        vec_uid_push(&regs[i]->curr_ents, uid);
        kh_put(name, s_dirty, names[i], &(int){0});
    }
}

static void region_update_ents(const char *name, struct region *reg)
{
    struct entity *ents[1024];
    size_t nents;

    switch(reg->type) {
    case REGION_CIRCLE: {
        nents = G_Pos_EntsInCircle(reg->pos, reg->radius, ents, ARR_SIZE(ents));
        break;
    }
    case REGION_RECTANGLE: {
        vec2_t xz_min = (vec2_t){reg->pos.x - reg->xlen/2.0f, reg->pos.z - reg->zlen/2.0f};
        vec2_t xz_max = (vec2_t){reg->pos.x + reg->xlen/2.0f, reg->pos.z + reg->zlen/2.0f};
        nents = G_Pos_EntsInRect(xz_min, xz_max, ents, ARR_SIZE(ents));
        break;
    }
    default: assert(0);
    }

    vec_uid_reset(&reg->curr_ents);
    for(int i = 0; i < nents; i++) {
        if(ents[i]->flags & ENTITY_FLAG_MARKER)
            continue;
        if(ents[i]->flags & ENTITY_FLAG_ZOMBIE)
            continue;
        vec_uid_push(&reg->curr_ents, ents[i]->uid);
    }

    khiter_t k = kh_get(region, s_regions, name);
    assert(k != kh_end(s_regions));
    kh_put(name, s_dirty, kh_key(s_regions, k), &(int){0});
}

static vec2_t region_ss_pos(vec2_t pos)
{
    int width, height;
    Engine_WinDrawableSize(&width, &height);

    float y = M_HeightAtPoint(s_map, M_ClampedMapCoordinate(s_map, pos));
    vec4_t pos_homo = (vec4_t) { pos.x, y, pos.z, 1.0f };

    const struct camera *cam = G_GetActiveCamera();
    mat4x4_t view, proj;
    Camera_MakeViewMat(cam, &view);
    Camera_MakeProjMat(cam, &proj);

    vec4_t clip, tmp;
    PFM_Mat4x4_Mult4x1(&view, &pos_homo, &tmp);
    PFM_Mat4x4_Mult4x1(&proj, &tmp, &clip);
    vec3_t ndc = (vec3_t){ clip.x / clip.w, clip.y / clip.w, clip.z / clip.w };

    float screen_x = (ndc.x + 1.0f) * width/2.0f;
    float screen_y = height - ((ndc.y + 1.0f) * height/2.0f);
    return (vec2_t){screen_x, screen_y};
}

static void region_notify_changed(const char *name, struct region *reg)
{
    size_t n = reg->curr_ents.size;
    size_t m = reg->prev_ents.size;

    qsort(reg->curr_ents.array, n, sizeof(uint32_t), compare_uint32s);
    qsort(reg->prev_ents.array, m, sizeof(uint32_t), compare_uint32s);

    /* use the algorithm for finding the symmetric difference 
     * of two sorted arrays: */
    size_t nchanged = 0;
    int i = 0, j = 0;
    while(i < n && j < m) {

        if(reg->curr_ents.array[i] < reg->prev_ents.array[j]) {

            const char *arg = pf_strdup(name);
            vec_str_push(&s_eventargs, arg);

            uint32_t uid = reg->curr_ents.array[i];
            E_Entity_Notify(EVENT_ENTERED_REGION, uid, (void*)arg, ES_ENGINE);

            i++;
            nchanged++;

        }else if(reg->prev_ents.array[j] < reg->curr_ents.array[i]) {

            const char *arg = pf_strdup(name);
            vec_str_push(&s_eventargs, arg);

            uint32_t uid = reg->prev_ents.array[j];
            E_Entity_Notify(EVENT_EXITED_REGION, uid, (void*)arg, ES_ENGINE);

            j++;
            nchanged++;

        }else{

            i++;
            j++;
        }
    }

    while(i < n) {
    
        const char *arg = pf_strdup(name);
        vec_str_push(&s_eventargs, arg);

        uint32_t uid = reg->curr_ents.array[i];
        E_Entity_Notify(EVENT_ENTERED_REGION, uid, (void*)arg, ES_ENGINE);

        i++;
        nchanged++;
    }

    while(j < m) {
    
        const char *arg = pf_strdup(name);
        vec_str_push(&s_eventargs, arg);

        uint32_t uid = reg->prev_ents.array[j];
        E_Entity_Notify(EVENT_EXITED_REGION, uid, (void*)arg, ES_ENGINE);

        j++;
        nchanged++;
    }

    if(nchanged) {
        S_Region_NotifyContentsChanged(name);
    }

    vec_uid_reset(&reg->prev_ents);
    vec_uid_copy(&reg->prev_ents, &reg->curr_ents);
}

static void on_render_3d(void *user, void *event)
{
    if(!s_render)
        return;

    const float width = 0.5f;
    const vec3_t red = (vec3_t){1.0f, 0.0f, 0.0f};

    const char *key;
    struct region reg;

    kh_foreach(s_regions, key, reg, {

        switch(reg.type) {
        case REGION_CIRCLE: {

            R_PushCmd((struct rcmd){
                .func = R_GL_DrawSelectionCircle,
                .nargs = 5,
                .args = {
                    R_PushArg(&reg.pos, sizeof(reg.pos)),
                    R_PushArg(&reg.radius, sizeof(reg.radius)),
                    R_PushArg(&width, sizeof(width)),
                    R_PushArg(&red, sizeof(red)),
                    (void*)G_GetPrevTickMap(),
                },
            });
            break;
        }
        case REGION_RECTANGLE: {

            vec2_t corners[4] = {
                (vec2_t){reg.pos.x + reg.xlen/2.0f, reg.pos.z - reg.zlen/2.0f},
                (vec2_t){reg.pos.x - reg.xlen/2.0f, reg.pos.z - reg.zlen/2.0f},
                (vec2_t){reg.pos.x - reg.xlen/2.0f, reg.pos.z + reg.zlen/2.0f},
                (vec2_t){reg.pos.x + reg.xlen/2.0f, reg.pos.z + reg.zlen/2.0f},
            };
            R_PushCmd((struct rcmd){
                .func = R_GL_DrawQuad,
                .nargs = 4,
                .args = {
                    R_PushArg(corners, sizeof(corners)),
                    R_PushArg(&width, sizeof(width)),
                    R_PushArg(&red, sizeof(red)),
                    (void*)G_GetPrevTickMap(),
                },
            });
            break;
        }
        default: assert(0);
        }

        float len = strlen(key) * 7.5f;
        vec2_t ss_pos = region_ss_pos(reg.pos);
        struct rect bounds = (struct rect){ss_pos.x - len/2.0, ss_pos.y, len, 16};
        struct rgba color = (struct rgba){255, 0, 0, 255};
        UI_DrawText(key, bounds, color);
    });
}

/*****************************************************************************/
/* EXTERN FUNCTIONS                                                          */
/*****************************************************************************/

bool G_Region_Init(const struct map *map)
{
    s_regions = kh_init(region);
    if(!s_regions)
        goto fail_regions;

    s_dirty = kh_init(name);
    if(!s_dirty)
        goto fail_dirty;

    struct map_resolution res;
    M_GetResolution(map, &res);

    s_intersecting = calloc(res.chunk_w * res.chunk_h, sizeof(vec_str_t));
    if(!s_intersecting)
        goto fail_intersecting;

    for(int i = 0; i < res.chunk_w * res.chunk_h; i++) {
        vec_str_t *vec = ((vec_str_t*)s_intersecting) + i;
        vec_str_init(vec);
    }

    vec_str_init(&s_eventargs);
    E_Global_Register(EVENT_RENDER_3D_POST, on_render_3d, NULL, G_ALL);
    s_map = map;
    return true;

fail_intersecting:
    kh_destroy(name, s_dirty);
fail_dirty:
    kh_destroy(region, s_regions);
fail_regions:
    return false;
}

void G_Region_Shutdown(void)
{
    struct map_resolution res;
    M_GetResolution(s_map, &res);

    for(int i = 0; i < res.chunk_w * res.chunk_h; i++) {
        vec_str_t *vec = ((vec_str_t*)s_intersecting) + i;
        vec_str_destroy(vec);
    }
    free(s_intersecting);

    const char *key;
    struct region reg;

    kh_foreach(s_regions, key, reg, {
        free((void*)key);
        vec_uid_destroy(&reg.curr_ents);
        vec_uid_destroy(&reg.prev_ents);
    });

    for(int i = 0; i < vec_size(&s_eventargs); i++) {
        free((void*)vec_AT(&s_eventargs, i));
    }
    vec_str_destroy(&s_eventargs);

    kh_destroy(name, s_dirty);
    kh_destroy(region, s_regions);
    E_Global_Unregister(EVENT_RENDER_3D_POST, on_render_3d);
    s_map = NULL;
}

bool G_Region_AddCircle(const char *name, vec2_t pos, float radius)
{
    struct region newreg = (struct region) {
        .type = REGION_CIRCLE,
        .radius = radius,
        .pos = pos
    };
    vec_uid_init(&newreg.curr_ents);
    vec_uid_init(&newreg.prev_ents);

    if(!region_add(name, newreg))
        return false;

    region_update_ents(name, &newreg);
    return true;
}

bool G_Region_AddRectangle(const char *name, vec2_t pos, float xlen, float zlen)
{
    struct region newreg = (struct region) {
        .type = REGION_RECTANGLE,
        .xlen = xlen,
        .zlen = zlen,
        .pos = pos
    };
    vec_uid_init(&newreg.curr_ents);
    vec_uid_init(&newreg.prev_ents);

    if(!region_add(name, newreg))
        return false;

    region_update_ents(name, &newreg);
    return true;
}

void G_Region_Remove(const char *name)
{
    khiter_t k = kh_get(region, s_regions, name);
    if(k == kh_end(s_regions))
        return;

    const char *key = kh_key(s_regions, k);
    struct region *reg = &kh_value(s_regions, k);

    for(int i = 0; i < vec_size(&reg->curr_ents); i++) {

        const char *arg = pf_strdup(name);
        vec_str_push(&s_eventargs, arg);

        uint32_t uid = vec_AT(&reg->curr_ents, i);
        E_Entity_Notify(EVENT_EXITED_REGION, uid, (void*)arg, ES_ENGINE);
    }

    region_update_intersecting(key, &kh_value(s_regions, k), REMOVE);
    vec_uid_destroy(&kh_val(s_regions, k).curr_ents);
    vec_uid_destroy(&kh_val(s_regions, k).prev_ents);
    kh_del(region, s_regions, k);

    k = kh_get(name, s_dirty, name);
    if(k != kh_end(s_dirty)) {
        kh_del(name, s_dirty, k);
    }

    free((void*)key);
}

bool G_Region_SetPos(const char *name, vec2_t pos)
{
    khiter_t k = kh_get(region, s_regions, name);
    if(k == kh_end(s_regions))
        return false;

    const char *key = kh_key(s_regions, k);
    struct region *reg = &kh_value(s_regions, k);

    vec2_t delta;
    PFM_Vec2_Sub(&reg->pos, &pos, &delta);
    if(PFM_Vec2_Len(&delta) <= EPSILON)
        return true;

    region_update_intersecting(key, reg, REMOVE);
    reg->pos = pos;
    region_update_intersecting(key, reg, ADD);

    region_update_ents(key, reg);
    return true;
}

bool G_Region_GetPos(const char *name, vec2_t *out)
{
    khiter_t k = kh_get(region, s_regions, name);
    if(k == kh_end(s_regions))
        return false;

    *out = kh_value(s_regions, k).pos;
    return true;
}

int G_Region_GetEnts(const char *name, size_t maxout, struct entity *ents[static maxout])
{
    khiter_t k = kh_get(region, s_regions, name);
    if(k == kh_end(s_regions))
        return 0;

    const struct region *reg = &kh_value(s_regions, k);
    size_t ret = 0;

    for(int i = 0; i < vec_size(&reg->curr_ents); i++) {

        struct entity *ent = G_EntityForUID(vec_AT(&reg->curr_ents, i));
        assert(ent);

        if(ret == maxout)
            return ret;
        ents[ret++] = ent;
    }
    return ret;
}

bool G_Region_ContainsEnt(const char *name, uint32_t uid)
{
    khiter_t k = kh_get(region, s_regions, name);
    if(k == kh_end(s_regions))
        return false;

    const struct region *reg = &kh_value(s_regions, k);
    for(int i = 0; i < vec_size(&reg->curr_ents); i++) {
        uint32_t curr = vec_AT(&reg->curr_ents, i);
        if(curr == uid)
            return true;
    }
    return false;
}

void G_Region_RemoveRef(uint32_t uid, vec2_t oldpos)
{
    regions_remove_ent(uid, oldpos);
}

void G_Region_AddRef(uint32_t uid, vec2_t newpos)
{
    regions_add_ent(uid, newpos);
}

void G_Region_RemoveEnt(uint32_t uid)
{
    vec2_t pos = G_Pos_GetXZ(uid);
    regions_remove_ent(uid, pos);
}

void G_Region_SetRender(bool on)
{
    s_render = on;
}

bool G_Region_GetRender(void)
{
    return s_render;
}

void G_Region_Update(void)
{
    for(int i = 0; i < vec_size(&s_eventargs); i++) {
        free((void*)vec_AT(&s_eventargs, i));
    }
    vec_str_reset(&s_eventargs);

    for(khiter_t k = kh_begin(s_dirty); k != kh_end(s_dirty); k++) {
        if(!kh_exist(s_dirty, k))
            continue;

        const char *key = kh_key(s_dirty, k);
        khiter_t l = kh_get(region, s_regions, key);
        assert(l != kh_end(s_regions));

        struct region *reg = &kh_value(s_regions, l);
        region_notify_changed(key, reg);
    }
    kh_clear(name, s_dirty);
}
