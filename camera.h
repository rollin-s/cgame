#ifndef CAMERA_H_3PIJHFMW
#define CAMERA_H_3PIJHFMW

#include "entity.h"
#include "mat3.h"

/* for now there's just one camera */

/* script_begin */

void camera_add(Entity ent);
void camera_remove();
Entity camera_get();

void camera_set_viewport_size(Vec2 dim); /* viewport size in world units */
Mat3 camera_get_inverse_view_matrix();

/* script_end */

void camera_update_all();
Mat3 *camera_get_inverse_view_matrix_ptr(); /* for quick GLSL binding */

#endif
