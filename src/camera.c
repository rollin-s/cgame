#include "camera.h"

#include "saveload.h"
#include "transform.h"
#include "game.h"
#include "assert.h"
#include "entitypool.h"

typedef struct Camera Camera;
struct Camera
{
    EntityPoolElem pool_elem;

    Scalar viewport_height;
};

static Entity curr_camera;

static Mat3 inverse_view_matrix; /* cached inverse view matrix */

static EntityPool *pool;

/* ------------------------------------------------------------------------- */

void camera_add(Entity ent)
{
    Camera *camera;

    if (entitypool_get(pool, ent))
        return;

    transform_add(ent);

    camera = entitypool_add(pool, ent);
    camera->viewport_height = 1.0;

    if (entity_eq(curr_camera, entity_nil))
        curr_camera = ent;
}
void camera_remove(Entity ent)
{
    entitypool_remove(pool, ent);

    if (entity_eq(curr_camera, ent))
        curr_camera = entity_nil;
}

void camera_set_current(Entity ent, bool current)
{
    if (current)
        curr_camera = ent;
    else if (entity_eq(curr_camera, ent))
        curr_camera = entity_nil;
}
bool camera_get_current(Entity ent)
{
    return entity_eq(curr_camera, ent);
}
void camera_set_current_camera(Entity ent)
{
    curr_camera = ent;
}
Entity camera_get_current_camera()
{
    return curr_camera;
}

void camera_set_viewport_height(Entity ent, Scalar height)
{
    Camera *camera = entitypool_get(pool, ent);
    assert(camera);
    camera->viewport_height = height;
}
Scalar camera_get_viewport_height(Entity ent)
{
    Camera *camera = entitypool_get(pool, ent);
    assert(camera);
    return camera->viewport_height;
}

Mat3 camera_get_inverse_view_matrix()
{
    return inverse_view_matrix;
}

const Mat3 *camera_get_inverse_view_matrix_ptr()
{
    return &inverse_view_matrix;
}

Vec2 camera_world_to_pixels(Vec2 p)
{
    return game_unit_to_pixels(camera_world_to_unit(p));
}
Vec2 camera_world_to_unit(Vec2 p)
{
    /* use cached inverse view matrix */
    return mat3_transform(inverse_view_matrix, p);
}
Vec2 camera_pixels_to_world(Vec2 p)
{
    return camera_unit_to_world(game_pixels_to_unit(p));
}
Vec2 camera_unit_to_world(Vec2 p)
{
    if (!entity_eq(curr_camera, entity_nil))
        return transform_local_to_world(curr_camera, p);
    return p;
}

/* ------------------------------------------------------------------------- */

void camera_init()
{
    pool = entitypool_new(Camera);
    curr_camera = entity_nil;
    inverse_view_matrix = mat3_identity();
}
void camera_deinit()
{
    entitypool_free(pool);
}

void camera_update_all()
{
    Vec2 win_size;
    Scalar aspect;
    Camera *camera;
    Vec2 scale;

    entitypool_remove_destroyed(pool, camera_remove);
    
    if (entity_eq(curr_camera, entity_nil))
        inverse_view_matrix = mat3_identity();

    win_size = game_get_window_size();
    aspect = win_size.x / win_size.y;

    entitypool_foreach(camera, pool)
    {
        scale = vec2(0.5 * aspect * camera->viewport_height,
                     0.5 * camera->viewport_height);
        transform_set_scale(camera->pool_elem.ent, scale);
    }

    if (entity_eq(curr_camera, entity_nil))
        inverse_view_matrix = mat3_identity();
    else
        inverse_view_matrix = mat3_inverse(
            transform_get_world_matrix(curr_camera));
}

void camera_save_all(Serializer *s)
{
    Camera *camera;

    entity_save(&curr_camera, s);
    mat3_save(&inverse_view_matrix, s);

    entitypool_save_foreach(camera, pool, s)
        scalar_save(&camera->viewport_height, s);
}
void camera_load_all(Deserializer *s)
{
    Camera *camera;

    entity_load(&curr_camera, s);
    mat3_load(&inverse_view_matrix, s);

    entitypool_load_foreach(camera, pool, s)
        scalar_load(&camera->viewport_height, s);
}

