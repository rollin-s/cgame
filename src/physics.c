#include "physics.h"

#include <stdlib.h>
#define CP_DATA_POINTER_TYPE Entity
#include <chipmunk.h>

#include "error.h"
#include "array.h"
#include "entitypool.h"
#include "timing.h"
#include "transform.h"
#include "gfx.h"
#include "dirs.h"
#include "camera.h"
#include "edit.h"
#include "entitymap.h"

/* per-entity info */
typedef struct PhysicsInfo PhysicsInfo;
struct PhysicsInfo
{
    EntityPoolElem pool_elem;

    PhysicsBody type;

    /* store mass separately to convert to/from PB_DYNAMIC */
    Scalar mass;

    /* used to compute (angular) velocitiy for PB_KINEMATIC */
    cpVect last_pos;
    cpFloat last_ang;

    /* used to keep track of transform <-> physics update */
    unsigned int last_dirty_count;

    cpBody *body;
    Array *shapes;
    Array *collisions;
};

/* per-shape info for each shape attached to a physics entity */
typedef struct ShapeInfo ShapeInfo;
struct ShapeInfo
{
    PhysicsShape type;
    cpShape *shape;
};

static cpSpace *space;
static Scalar period = 1.0 / 60.0; /* 1.0 / simulation_frequency */
static EntityPool *pool;

static EntityMap *debug_draw_map;

/* ------------------------------------------------------------------------- */

/* chipmunk utilities */

static inline cpVect cpv_of_vec2(Vec2 v) { return cpv(v.x, v.y); }
static inline Vec2 vec2_of_cpv(cpVect v) { return vec2(v.x, v.y); }

static inline void _remove_body(cpBody *body)
{
    if (cpSpaceContainsBody(space, body))
        cpSpaceRemoveBody(space, body);
    cpBodyFree(body);
}
static inline void _remove_shape(cpShape *shape)
{
    if (cpSpaceContainsShape(space, shape))
        cpSpaceRemoveShape(space, shape);
    cpShapeFree(shape);
}

/* ------------------------------------------------------------------------- */

void physics_set_gravity(Vec2 g)
{
    cpSpaceSetGravity(space, cpv_of_vec2(g));
}
Vec2 physics_get_gravity()
{
    return vec2_of_cpv(cpSpaceGetGravity(space));
}

void physics_set_simulation_frequency(Scalar freq)
{
    period = 1.0 / freq;
}
Scalar physics_get_simulation_frequency()
{
    return 1.0 / period;
}

void physics_add(Entity ent)
{
    PhysicsInfo *info;

    if (entitypool_get(pool, ent))
        return; /* already has physics */

    transform_add(ent);

    info = entitypool_add(pool, ent);

    info->mass = 1.0;
    info->type = PB_DYNAMIC;

    /* create, init cpBody */
    info->body = cpSpaceAddBody(space, cpBodyNew(info->mass, 1.0));
    cpBodySetUserData(info->body, ent); /* for cpBody -> Entity mapping */
    cpBodySetPos(info->body, cpv_of_vec2(transform_get_position(ent)));
    cpBodySetAngle(info->body, transform_get_rotation(ent));
    info->last_dirty_count = transform_get_dirty_count(ent);

    /* initially no shapes */
    info->shapes = array_new(ShapeInfo);

    /* initialize last_pos/last_ang info for kinematic bodies */
    info->last_pos = cpBodyGetPos(info->body);
    info->last_ang = cpBodyGetAngle(info->body);

    info->collisions = NULL;
}

/* remove chipmunk stuff (doesn't remove from pool) */
static void _remove(PhysicsInfo *info)
{
    ShapeInfo *shapeInfo;

    array_foreach(shapeInfo, info->shapes)
        _remove_shape(shapeInfo->shape);
    array_free(info->shapes);

    _remove_body(info->body);
}

void physics_remove(Entity ent)
{
    PhysicsInfo *info;

    info = entitypool_get(pool, ent);
    if (!info)
        return;

    _remove(info);
    entitypool_remove(pool, ent);
}

bool physics_has(Entity ent)
{
    return entitypool_get(pool, ent) != NULL;
}

/* calculate moment for a single shape */
static Scalar _moment(cpBody *body, ShapeInfo *shapeInfo)
{
    Scalar mass = cpBodyGetMass(body);
    switch (shapeInfo->type)
    {
        case PS_CIRCLE:
            return cpMomentForCircle(mass, 0,
                                     cpCircleShapeGetRadius(shapeInfo->shape),
                                     cpCircleShapeGetOffset(shapeInfo->shape));

        case PS_POLYGON:
            return cpMomentForPoly(mass,
                                   cpPolyShapeGetNumVerts(shapeInfo->shape),
                                   ((cpPolyShape *) shapeInfo->shape)->verts,
                                   cpvzero);
    }
}

/* recalculate moment for whole body by adding up shape moments */
static void _recalculate_moment(PhysicsInfo *info)
{
    Scalar moment;
    ShapeInfo *shapeInfo;

    if (!info->body)
        return;
    if (array_length(info->shapes) == 0)
        return; /* can't set moment to zero, just leave it alone */

    moment = 0.0;
    array_foreach(shapeInfo, info->shapes)
        moment += _moment(info->body, shapeInfo);
    cpBodySetMoment(info->body, moment);
}

static void _set_type(PhysicsInfo *info, PhysicsBody type)
{
    if (info->type == type)
        return; /* already set */

    info->type = type;
    switch (type)
    {
        case PB_KINEMATIC:
            info->last_pos = cpBodyGetPos(info->body);
            info->last_ang = cpBodyGetAngle(info->body);
            /* fall through */

        case PB_STATIC:
            if (!cpBodyIsStatic(info->body))
            {
                cpSpaceRemoveBody(space, info->body);
                cpSpaceConvertBodyToStatic(space, info->body);
            }
            break;

        case PB_DYNAMIC:
            cpSpaceConvertBodyToDynamic(space, info->body, info->mass, 1.0);
            cpSpaceAddBody(space, info->body);
            _recalculate_moment(info);
            break;
    }
}
void physics_set_type(Entity ent, PhysicsBody type)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    _set_type(info, type);
}
PhysicsBody physics_get_type(Entity ent)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    return info->type;
}

void physics_debug_draw(Entity ent)
{
    entitymap_set(debug_draw_map, ent, true);
}


/* --- shape --------------------------------------------------------------- */

static unsigned int _shape_add(Entity ent, PhysicsShape type, cpShape *shape)
{
    PhysicsInfo *info;
    ShapeInfo *shapeInfo;

    info = entitypool_get(pool, ent);
    error_assert(info);

    /* init ShapeInfo */
    shapeInfo = array_add(info->shapes);
    shapeInfo->type = type;
    shapeInfo->shape = shape;

    /* init cpShape */
    cpShapeSetBody(shape, info->body);
    cpSpaceAddShape(space, shape);
    cpShapeSetFriction(shapeInfo->shape, 1);
    cpShapeSetUserData(shapeInfo->shape, ent);

    /* update moment */
    if (!cpBodyIsStatic(info->body))
    {
        if (array_length(info->shapes) > 1)
            cpBodySetMoment(info->body, _moment(info->body, shapeInfo)
                            + cpBodyGetMoment(info->body));
        else
            cpBodySetMoment(info->body, _moment(info->body, shapeInfo));
    }

    return array_length(info->shapes) - 1;
}
unsigned int physics_shape_add_circle(Entity ent, Scalar r,
                                      Vec2 offset)
{
    cpShape *shape = cpCircleShapeNew(NULL, r, cpv_of_vec2(offset));
    return _shape_add(ent, PS_CIRCLE, shape);
}
unsigned int physics_shape_add_box(Entity ent, BBox b, Scalar r)
{
    cpShape *shape = cpBoxShapeNew3(NULL, cpBBNew(b.min.x, b.min.y,
                                                  b.max.x, b.max.y), r);
    return _shape_add(ent, PS_POLYGON, shape);
}
unsigned int physics_shape_add_poly(Entity ent,
                                    unsigned int nverts,
                                    const Vec2 *verts,
                                    Scalar r)
{
    unsigned int i;
    cpVect *cpverts;
    cpShape *shape;

    cpverts = malloc(nverts * sizeof(cpVect));
    for (i = 0; i < nverts; ++i)
        cpverts[i] = cpv_of_vec2(verts[i]);
    nverts = cpConvexHull(nverts, cpverts, NULL, NULL, 0);
    shape = cpPolyShapeNew2(NULL, nverts, cpverts, cpvzero, r);
    free(cpverts);
    return _shape_add(ent, PS_POLYGON, shape);
}

unsigned int physics_get_num_shapes(Entity ent)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    return array_length(info->shapes);
}
PhysicsShape physics_shape_get_type(Entity ent, unsigned int i)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    error_assert(i < array_length(info->shapes));
    return array_get_val(ShapeInfo, info->shapes, i).type;
}
void physics_shape_remove(Entity ent, unsigned int i)
{
    PhysicsInfo *info;
    ShapeInfo *shapeInfo;

    info = entitypool_get(pool, ent);
    error_assert(info);

    if (i >= array_length(info->shapes))
        return;

    shapeInfo = array_get(info->shapes, i);
    _remove_shape(array_get_val(ShapeInfo, info->shapes, i).shape);
    array_quick_remove(info->shapes, i);
    _recalculate_moment(info);
}

static ShapeInfo *_get_shape(PhysicsInfo *info, unsigned int i)
{
    error_assert(i < array_length(info->shapes),
                 "shape index should be in range");
    return array_get(info->shapes, i);
}

int physics_poly_get_num_verts(Entity ent, unsigned int i)
{
    PhysicsInfo *info;
    info = entitypool_get(pool, ent);
    error_assert(info);
    return cpPolyShapeGetNumVerts(_get_shape(info, i)->shape);
}

unsigned int physics_convex_hull(unsigned int nverts, Vec2 *verts)
{
    cpVect *cpverts;
    unsigned int i;

    cpverts = malloc(nverts * sizeof(cpVect));
    for (i = 0; i < nverts; ++i)
        cpverts[i] = cpv_of_vec2(verts[i]);
    nverts = cpConvexHull(nverts, cpverts, NULL, NULL, 0);
    for (i = 0; i < nverts; ++i)
        verts[i] = vec2_of_cpv(cpverts[i]);
    free(cpverts);
    return nverts;
}

void physics_shape_set_surface_velocity(Entity ent,
                                        unsigned int i,
                                        Vec2 v)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    cpShapeSetSurfaceVelocity(_get_shape(info, i)->shape, cpv_of_vec2(v));
}
Vec2 physics_shape_get_surface_velocity(Entity ent,
                                        unsigned int i)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    return vec2_of_cpv(cpShapeGetSurfaceVelocity(_get_shape(info, i)->shape));
}

void physics_shape_set_sensor(Entity ent,
                              unsigned int i,
                              bool sensor)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    cpShapeSetSensor(_get_shape(info, i)->shape, sensor);
}
bool physics_shape_get_sensor(Entity ent,
                              unsigned int i)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    return cpShapeGetSensor(_get_shape(info, i)->shape);
}

/* --- dynamics ------------------------------------------------------------ */

void physics_set_mass(Entity ent, Scalar mass)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);

    if (mass <= SCALAR_EPSILON)
        return;

    cpBodySetMass(info->body, info->mass = mass);
    _recalculate_moment(info);
}
Scalar physics_get_mass(Entity ent)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    return info->mass;
}

void physics_set_freeze_rotation(Entity ent, bool freeze)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);

    if (freeze)
    {
        cpBodySetAngVel(info->body, 0);
        cpBodySetMoment(info->body, SCALAR_INFINITY);
    }
    else
        _recalculate_moment(info);
}
bool physics_get_freeze_rotation(Entity ent)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);

    /* TODO: do this a better way? maybe store separate flag */
    return cpBodyGetMoment(info->body) == SCALAR_INFINITY;
}

void physics_set_velocity(Entity ent, Vec2 vel)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    cpBodySetVel(info->body, cpv_of_vec2(vel));
}
Vec2 physics_get_velocity(Entity ent)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    return vec2_of_cpv(cpBodyGetVel(info->body));
}
void physics_set_force(Entity ent, Vec2 force)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    cpBodySetForce(info->body, cpv_of_vec2(force));
}
Vec2 physics_get_force(Entity ent)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    return vec2_of_cpv(cpBodyGetForce(info->body));
}

void physics_set_angular_velocity(Entity ent, Scalar ang_vel)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    cpBodySetAngVel(info->body, ang_vel);
}
Scalar physics_get_angular_velocity(Entity ent)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    return cpBodyGetAngVel(info->body);
}
void physics_set_torque(Entity ent, Scalar torque)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    cpBodySetTorque(info->body, torque);
}
Scalar physics_get_torque(Entity ent)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    return cpBodyGetTorque(info->body);
}

void physics_set_velocity_limit(Entity ent, Scalar lim)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    cpBodySetVelLimit(info->body, lim);
}
Scalar physics_get_velocity_limit(Entity ent)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    return cpBodyGetVelLimit(info->body);
}
void physics_set_angular_velocity_limit(Entity ent, Scalar lim)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    cpBodySetAngVelLimit(info->body, lim);
}
Scalar physics_get_angular_velocity_limit(Entity ent)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    return cpBodyGetAngVelLimit(info->body);
}

void physics_reset_forces(Entity ent)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    cpBodyResetForces(info->body);
}
void physics_apply_force(Entity ent, Vec2 force)
{
    physics_apply_force_at(ent, force, vec2_zero);
}
void physics_apply_force_at(Entity ent, Vec2 force, Vec2 at)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    cpBodyApplyForce(info->body, cpv_of_vec2(force), cpv_of_vec2(at));
}
void physics_apply_impulse(Entity ent, Vec2 impulse)
{
    physics_apply_impulse_at(ent, impulse, vec2_zero);
}
void physics_apply_impulse_at(Entity ent, Vec2 impulse, Vec2 at)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);
    cpBodyApplyImpulse(info->body, cpv_of_vec2(impulse), cpv_of_vec2(at));
}

/* --- collisions ---------------------------------------------------------- */

static void _add_collision(cpBody *body, cpArbiter *arbiter, void *collisions)
{
    cpBody *ba, *bb;
    Collision *col;

    /* get in right order */
    cpArbiterGetBodies(arbiter, &ba, &bb);
    if (bb == body)
    {
        ba = body;
        bb = ba;
    }

    /* save collision */
    col = array_add(collisions);
    col->a = cpBodyGetUserData(ba);
    col->b = cpBodyGetUserData(bb);
}
static void _update_collisions(PhysicsInfo *info)
{
    if (info->collisions)
        return;

    /* gather collisions */
    info->collisions = array_new(Collision);
    cpBodyEachArbiter(info->body, _add_collision, info->collisions);
}

unsigned int physics_get_num_collisions(Entity ent)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);

    _update_collisions(info);
    return array_length(info->collisions);
}
Collision *physics_get_collisions(Entity ent)
{
    PhysicsInfo *info = entitypool_get(pool, ent);
    error_assert(info);

    _update_collisions(info);
    return array_begin(info->collisions);
}


/* --- queries ------------------------------------------------------------- */

NearestResult physics_nearest(Vec2 point, Scalar max_dist)
{
    cpNearestPointQueryInfo info;
    NearestResult res;

    if (!cpSpaceNearestPointQueryNearest(space, cpv_of_vec2(point), max_dist,
                                         CP_ALL_LAYERS, CP_NO_GROUP, &info))
    {
        /* no result */
        res.ent = entity_nil;
        return res;
    }

    res.ent = cpShapeGetUserData(info.shape);
    res.p = vec2_of_cpv(info.p);
    res.d = info.d;
    res.g = vec2_of_cpv(info.g);
    return res;
}

/* --- init/deinit --------------------------------------------------------- */

static GLuint program;
static GLuint vao;
static GLuint vbo;

void physics_init()
{
    /* init pools, maps */
    pool = entitypool_new(PhysicsInfo);
    debug_draw_map = entitymap_new(false);

    /* init cpSpace */
    space = cpSpaceNew();
    cpSpaceSetGravity(space, cpv(0, -9.8));

    /* init draw stuff */
    program = gfx_create_program(data_path("phypoly.vert"),
                                 NULL,
                                 data_path("phypoly.frag"));
    glUseProgram(program);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    gfx_bind_vertex_attrib(program, GL_FLOAT, 2, "position", Vec2, x);
}
void physics_deinit()
{
    PhysicsInfo *info;

    /* clean up draw stuff */
    glDeleteProgram(program);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);

    /* remove all bodies, shapes */
    entitypool_foreach(info, pool)
        _remove(info);

    /* deinit cpSpace */
    cpSpaceFree(space);

    /* deinit pools, maps */
    entitymap_free(debug_draw_map);
    entitypool_free(pool);
}

/* --- update -------------------------------------------------------------- */

/* step the space with fixed time step */
static void _step()
{
    static Scalar remain = 0.0;

    remain += timing_dt;
    while (remain >= period)
    {
        cpSpaceStep(space, period);
        remain -= period;
    }
}

static void _update_kinematics()
{
    PhysicsInfo *info;
    cpVect pos;
    cpFloat ang;
    Scalar invdt;
    Entity ent;

    if (timing_dt <= FLT_EPSILON)
        return;
    invdt = 1 / timing_dt;

    entitypool_foreach(info, pool)
        if (info->type == PB_KINEMATIC)
        {
            ent = info->pool_elem.ent;

            /* move to transform */
            pos = cpv_of_vec2(transform_get_position(ent));
            ang = transform_get_rotation(ent);
            cpBodySetPos(info->body, pos);
            cpBodySetAngle(info->body, ang);
            info->last_dirty_count = transform_get_dirty_count(ent);

            /* update linear, angular velocities based on delta */
            cpBodySetVel(info->body,
                         cpvmult(cpvsub(pos, info->last_pos), invdt));
            cpBodySetAngVel(info->body, (ang - info->last_ang) * invdt);
            cpSpaceReindexShapesForBody(space, info->body);

            /* save current state for next computation */
            info->last_pos = pos;
            info->last_ang = ang;
        }
}
void physics_update_all()
{
    PhysicsInfo *info;
    Entity ent;

    entitypool_remove_destroyed(pool, physics_remove);

    entitymap_clear(debug_draw_map);

    /* simulate */
    if (!timing_get_paused())
    {
        _update_kinematics();
        _step();
    }

    /* synchronize transform <-> physics */
    entitypool_foreach(info, pool)
    {
        ent = info->pool_elem.ent;

        /* if transform is dirtier, move to it, else overwrite it */
        if (transform_get_dirty_count(ent) != info->last_dirty_count)
        {
            cpBodySetVel(info->body, cpvzero);
            cpBodySetAngVel(info->body, 0.0f);
            cpBodySetPos(info->body, cpv_of_vec2(transform_get_position(ent)));
            cpBodySetAngle(info->body, transform_get_rotation(ent));
            cpSpaceReindexShapesForBody(space, info->body);
        }
        else if (info->type == PB_DYNAMIC)
        {
            transform_set_position(ent, vec2_of_cpv(cpBodyGetPos(info->body)));
            transform_set_rotation(ent, cpBodyGetAngle(info->body));
        }

        info->last_dirty_count = transform_get_dirty_count(ent);
    }
}

void physics_post_update_all()
{
    PhysicsInfo *info;

    entitypool_remove_destroyed(pool, physics_remove);

    /* clear collisions */
    entitypool_foreach(info, pool)
        if (info->collisions)
        {
            array_free(info->collisions);
            info->collisions = NULL;
        }
}

/* --- draw ---------------------------------------------------------------- */

static void _circle_draw(PhysicsInfo *info, ShapeInfo *shapeInfo)
{
    static Vec2 verts[] = {
        {  1.0,  0.0 }, {  0.7071,  0.7071 },
        {  0.0,  1.0 }, { -0.7071,  0.7071 },
        { -1.0,  0.0 }, { -0.7071, -0.7071 },
        {  0.0, -1.0 }, {  0.7071, -0.7071 },
    }, offset;
    const unsigned int nverts = sizeof(verts) / sizeof(verts[0]);
    Scalar r;
    Mat3 wmat;

    wmat = transform_get_world_matrix(info->pool_elem.ent);
    offset = vec2_of_cpv(cpCircleShapeGetOffset(shapeInfo->shape));
    r = cpCircleShapeGetRadius(shapeInfo->shape);

    glUniformMatrix3fv(glGetUniformLocation(program, "wmat"),
                       1, GL_FALSE, (const GLfloat *) &wmat);
    glUniform2fv(glGetUniformLocation(program, "offset"),
                 1, (const GLfloat *) &offset);
    glUniform1f(glGetUniformLocation(program, "radius"), r);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, nverts * sizeof(Vec2),
                 verts, GL_STREAM_DRAW);
    glDrawArrays(GL_LINE_LOOP, 0, nverts);
    glDrawArrays(GL_POINTS, 0, nverts);
}

static void _polygon_draw(PhysicsInfo *info, ShapeInfo *shapeInfo)
{
    unsigned int i, nverts;
    Vec2 *verts;
    Mat3 wmat;

    wmat = transform_get_world_matrix(info->pool_elem.ent);
    glUniformMatrix3fv(glGetUniformLocation(program, "wmat"),
                       1, GL_FALSE, (const GLfloat *) &wmat);

    glUniformMatrix3fv(glGetUniformLocation(program, "wmat"),
                       1, GL_FALSE, (const GLfloat *) &wmat);
    glUniform2f(glGetUniformLocation(program, "offset"), 0, 0);
    glUniform1f(glGetUniformLocation(program, "radius"), 1);

    /* copy as Vec2 array */
    nverts = cpPolyShapeGetNumVerts(shapeInfo->shape);
    verts = malloc(nverts * sizeof(Vec2));
    for (i = 0; i < nverts; ++i)
        verts[i] = vec2_of_cpv(cpPolyShapeGetVert(shapeInfo->shape, i));

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, nverts * sizeof(Vec2),
                 verts, GL_STREAM_DRAW);
    glDrawArrays(GL_LINE_LOOP, 0, nverts);
    glDrawArrays(GL_POINTS, 0, nverts);

    free(verts);
}

void physics_draw_all()
{
    PhysicsInfo *info;
    ShapeInfo *shapeInfo;

    if (!edit_get_enabled())
        return;

    /* bind program, update uniforms */
    glUseProgram(program);
    glUniformMatrix3fv(glGetUniformLocation(program, "inverse_view_matrix"),
                       1, GL_FALSE,
                       (const GLfloat *) camera_get_inverse_view_matrix_ptr());

    /* draw! */
    entitypool_foreach(info, pool)
        if (entitymap_get(debug_draw_map, info->pool_elem.ent))
            array_foreach(shapeInfo, info->shapes)
                switch(shapeInfo->type)
                {
                    case PS_CIRCLE:
                        _circle_draw(info, shapeInfo);
                        break;

                    case PS_POLYGON:
                        _polygon_draw(info, shapeInfo);
                        break;
                }
}

/* --- save/load ----------------------------------------------------------- */

/* chipmunk data save/load helpers */
static void _cpv_save(cpVect *cv, const char *n, Store *s)
{
    Vec2 v = vec2_of_cpv(*cv);
    vec2_save(&v, n, s);
}
static bool _cpv_load(cpVect *cv, const char *n, cpVect d, Store *s)
{
    Vec2 v;
    if (vec2_load(&v, n, vec2_zero, s))
    {
        *cv = cpv_of_vec2(v);
        return true;
    }
    *cv = d;
    return false;
}
static void _cpf_save(cpFloat *cf, const char *n, Store *s)
{
    Scalar f = *cf;
    scalar_save(&f, n, s);
}
static bool _cpf_load(cpFloat *cf, const char *n, cpFloat d, Store *s)
{
    Scalar f;
    bool r = scalar_load(&f, n, d, s);
    *cf = f;
    return r;
}

/* properties aren't set if missing, so defaults don't really matter */
#define _cpv_default cpvzero
#define _cpf_default 0.0f
#define bool_default false
#define uint_default 0

/*
 * note that UserData isn't directly save/load'd -- it is restored to
 * the Entity value on load separately
 */

/* some hax to reduce typing for body properties save/load */
#define body_prop_save(type, f, n, prop)        \
    {                                           \
        type v;                                 \
        v = cpBodyGet##prop(info->body);        \
            f##_save(&v, n, body_s);            \
    }
#define body_prop_load(type, f, n, prop)                \
    {                                                   \
        type v;                                         \
        if (f##_load(&v, n, f##_default, body_s))       \
            cpBodySet##prop(info->body, v);             \
    }
#define body_props_saveload(saveload)                                   \
    body_prop_##saveload(cpFloat, _cpf, "mass", Mass);                  \
    body_prop_##saveload(cpFloat, _cpf, "moment", Moment);              \
    /* body_prop_##saveload(cpVect, _cpv, Pos); */                      \
    body_prop_##saveload(cpVect, _cpv, "vel", Vel);                     \
    body_prop_##saveload(cpVect, _cpv, "force", Force);                 \
    /* body_prop_##saveload(cpFloat, _cpf, Angle); */                   \
    body_prop_##saveload(cpFloat, _cpf, "ang_vel", AngVel);             \
    body_prop_##saveload(cpFloat, _cpf, "torque", Torque);              \
    body_prop_##saveload(cpFloat, _cpf, "vel_limit", VelLimit);         \
    body_prop_##saveload(cpFloat, _cpf, "ang_vel_limit", AngVelLimit);  \

/* save/load for just the body in a PhysicsInfo */
static void _body_save(PhysicsInfo *info, Store *s)
{
    Store *body_s;

    if (store_child_save(&body_s, "body", s))
        body_props_saveload(save);
}
static void _body_load(PhysicsInfo *info, Store *s)
{
    Store *body_s;
    Entity ent;
    PhysicsBody type;

    error_assert(store_child_load(&body_s, "body", s),
                 "physics entry must have a saved body");

    /* create, restore properties */
    ent = info->pool_elem.ent;
    info->body = cpSpaceAddBody(space, cpBodyNew(info->mass, 1.0));
    body_props_saveload(load);
    cpBodySetUserData(info->body, ent);

    /* force type change if non-default */
    type = info->type;
    info->type = PB_DYNAMIC;
    _set_type(info, type);

    /* restore position, angle based on transform */
    cpBodySetPos(info->body, cpv_of_vec2(transform_get_position(ent)));
    cpBodySetAngle(info->body, transform_get_rotation(ent));
    info->last_dirty_count = transform_get_dirty_count(info->pool_elem.ent);
}

/* save/load for special properties of each shape type */

static void _circle_save(PhysicsInfo *info, ShapeInfo *shapeInfo,
                         Store *s)
{
    cpFloat radius;
    cpVect offset;

    radius = cpCircleShapeGetRadius(shapeInfo->shape);
    _cpf_save(&radius, "radius", s);
    offset = cpCircleShapeGetOffset(shapeInfo->shape);
    _cpv_save(&offset, "offset", s);
}
static void _circle_load(PhysicsInfo *info, ShapeInfo *shapeInfo,
                         Store *s)
{
    cpFloat radius;
    cpVect offset;

    _cpf_load(&radius, "radius", 1, s);
    _cpv_load(&offset, "offset", cpvzero, s);

    shapeInfo->shape = cpCircleShapeNew(info->body, radius, offset);
    cpSpaceAddShape(space, shapeInfo->shape);
}

static void _polygon_save(PhysicsInfo *info, ShapeInfo *shapeInfo,
                          Store *s)
{
    Store *verts_s;
    unsigned int n, i;
    Scalar r;
    cpVect v;

    n = cpPolyShapeGetNumVerts(shapeInfo->shape);
    uint_save(&n, "num_verts", s);

    r = cpPolyShapeGetRadius(shapeInfo->shape);
    scalar_save(&r, "radius", s);

    if (store_child_save(&verts_s, "verts", s))
        for (i = 0; i < n; ++i)
        {
            v = cpPolyShapeGetVert(shapeInfo->shape, i);
            _cpv_save(&v, NULL, verts_s);
        }
}
static void _polygon_load(PhysicsInfo *info, ShapeInfo *shapeInfo,
                          Store *s)
{
    Store *verts_s;
    unsigned int n, i;
    Scalar r;
    cpVect *vs;

    error_assert(uint_load(&n, "num_verts", 0, s),
                 "polygon shape type must have saved number of vertices");
    scalar_load(&r, "radius", 0, s);

    error_assert(store_child_load(&verts_s, "verts", s),
                 "polygon shape type must have saved list of vertices");
    vs = malloc(n * sizeof(cpVect));
    for (i = 0; i < n; ++i)
        if (!_cpv_load(&vs[i], NULL, cpvzero, verts_s))
            error("polygon shape type saved number of vertices doesn't match"
                  "size of saved list of vertices");
    shapeInfo->shape = cpPolyShapeNew2(info->body, n, vs, cpvzero, r);
    cpSpaceAddShape(space, shapeInfo->shape);
    free(vs);
}

/* some hax to reduce typing for shape properties save/load */
#define shape_prop_save(type, f, n, prop)       \
    {                                           \
        type v;                                 \
        v = cpShapeGet##prop(shapeInfo->shape); \
            f##_save(&v, n, shape_s);           \
    }
#define shape_prop_load(type, f, n, prop)               \
    {                                                   \
        type v;                                         \
        if (f##_load(&v, n, f##_default, shape_s))      \
            cpShapeSet##prop(shapeInfo->shape, v);      \
    }
#define shape_props_saveload(saveload)                                  \
    shape_prop_##saveload(bool, bool, "sensor", Sensor);                \
    shape_prop_##saveload(cpFloat, _cpf, "elasticity", Elasticity);     \
    shape_prop_##saveload(cpFloat, _cpf, "friction", Friction);         \
    shape_prop_##saveload(cpVect, _cpv, "surface_velocity", SurfaceVelocity); \
    shape_prop_##saveload(unsigned int, uint, "collision_type",         \
                          CollisionType);                               \
    shape_prop_##saveload(unsigned int, uint, "group", Group);          \
    shape_prop_##saveload(unsigned int, uint, "layers", Layers);        \

/* save/load for all shapes in a PhysicsInfo */
static void _shapes_save(PhysicsInfo *info, Store *s)
{
    Store *t, *shape_s;
    ShapeInfo *shapeInfo;

    if (store_child_save(&t, "shapes", s))
        array_foreach(shapeInfo, info->shapes)
            if (store_child_save(&shape_s, NULL, t))
            {
                /* type-specific */
                enum_save(&shapeInfo->type, "type", shape_s);
                switch (shapeInfo->type)
                {
                    case PS_CIRCLE:
                        _circle_save(info, shapeInfo, shape_s);
                        break;
                    case PS_POLYGON:
                        _polygon_save(info, shapeInfo, shape_s);
                        break;
                }

                /* common */
                shape_props_saveload(save);
            }
}
static void _shapes_load(PhysicsInfo *info, Store *s)
{
    Store *t, *shape_s;
    ShapeInfo *shapeInfo;

    info->shapes = array_new(ShapeInfo);

    if (store_child_load(&t, "shapes", s))
        while (store_child_load(&shape_s, NULL, t))
        {
            shapeInfo = array_add(info->shapes);

            /* type-specific */
            enum_load(&shapeInfo->type, "type", PS_CIRCLE, shape_s);
            switch (shapeInfo->type)
            {
                case PS_CIRCLE:
                    _circle_load(info, shapeInfo, shape_s);
                    break;
                case PS_POLYGON:
                    _polygon_load(info, shapeInfo, shape_s);
                    break;
            }

            /* common */
            shape_props_saveload(load);
            cpShapeSetUserData(shapeInfo->shape, info->pool_elem.ent);
        }
}

/* 
 * save/load for all data in a PhysicsInfo other than the the actual body,
 * shapes is handled here, the rest is done in functions above
 */
void physics_save_all(Store *s)
{
    Store *t, *info_s;
    PhysicsInfo *info;

    if (store_child_save(&t, "physics", s))
        entitypool_save_foreach(info, info_s, pool, "pool", t)
        {
            enum_save(&info->type, "type", info_s);
            scalar_save(&info->mass, "mass", info_s);

            _body_save(info, info_s);
            _shapes_save(info, info_s);
        }
}
void physics_load_all(Store *s)
{
    Store *t, *info_s;
    PhysicsInfo *info;

    if (store_child_load(&t, "physics", s))
        entitypool_load_foreach(info, info_s, pool, "pool", t)
        {
            enum_load(&info->type, "type", PB_DYNAMIC, info_s);
            scalar_load(&info->mass, "mass", 1, info_s);

            _body_load(info, info_s);
            _shapes_load(info, info_s);

            info->collisions = NULL;

            /* set last_pos/last_ang info for kinematic bodies */
            info->last_pos = cpBodyGetPos(info->body);
            info->last_ang = cpBodyGetAngle(info->body);
        }
}

