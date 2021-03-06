#ifndef ENTITYPOOL_H
#define ENTITYPOOL_H

#include <stddef.h>

#include "entity.h"
#include "saveload.h"

/*
 * continuous in memory, may be relocated/shuffled so be careful
 */

typedef struct EntityPool EntityPool;

/*
 * this struct must be at the top of pool elements:
 * 
 *     struct Data
 *     {
 *         EntityPoolElem pool_elem;
 *
 *         ...
 *     }
 *
 * values in it are metadata managed by EntityPool
 *
 * look at transform.c, sprite.c, etc. for examples
 */
typedef struct EntityPoolElem EntityPoolElem;
struct EntityPoolElem
{
    Entity ent; /* key for this element */
};

/* object_size is size per element */
EntityPool *entitypool_new_(size_t object_size);
#define entitypool_new(type) entitypool_new_(sizeof(type))
void entitypool_free(EntityPool *pool);

void *entitypool_add(EntityPool *pool, Entity ent);
void entitypool_remove(EntityPool *pool, Entity ent);
void *entitypool_get(EntityPool *pool, Entity ent); /* NULL if not mapped */

/* since elements are contiguoous, can iterate with pointers:
 *
 *     for (ptr = entitypool_begin(pool), end = entitypool_end(pool);
 *             ptr != end; ++ptr)
 *         ... use ptr ...
 *
 * NOTE: if you add/remove during iteration the pointers are invalidated
 */
void *entitypool_begin(EntityPool *pool);
void *entitypool_end(EntityPool *pool); /* one-past-end */
void *entitypool_nth(EntityPool *pool, unsigned int n); /* 0-indexed */

unsigned int entitypool_size(EntityPool *pool);

void entitypool_clear(EntityPool *pool);

/* compare is a comparator function like for qsort(3) */
void entitypool_sort(EntityPool *pool,
                     int (*compar)(const void *, const void *));

/* elem must be /pointer to/ pointer to element */
void entitypool_elem_save(EntityPool *pool, void *elem, Store *s);
void entitypool_elem_load(EntityPool *pool, void *elem, Store *s);

/*
 * call 'func' on each destroyed Entity, generally done in
 * *_update_all() -- check transform.c, sprite.c, etc. for examples
 */
#define entitypool_remove_destroyed(pool, func)         \
    do                                                  \
    {                                                   \
        unsigned int __i;                               \
        EntityPoolElem *__e;                            \
        for (__i = 0; __i < entitypool_size(pool); )    \
        {                                               \
            __e = entitypool_nth(pool, __i);            \
            if (entity_destroyed(__e->ent))             \
                func(__e->ent);                         \
            else                                        \
                ++__i;                                  \
        }                                               \
    } while (0)

/*
 * can be used as:
 *
 *     entitypool_foreach(var, pool)
 *         ... use var ...
 *
 * here var must name a variable of type 'pointer to element' declared
 * before -- do not use this if adding/removing from pool while
 * iterating
 *
 * elements are visited in order of increasing index
 */
#define entitypool_foreach(var, pool)                                   \
    for (void *__end = (var = entitypool_begin(pool),                   \
                        entitypool_end(pool)); var != __end; ++var)

/*
 * save/load each element of an EntityPool -- var is the variable used
 * to iterate over the pool, var_s is the Store pointer to use for the
 * Stores created/loaded per element, pool is the EntityPool, n is the
 * name to save/load the entire pool under, and finally s is the
 * parent pool to save under
 *
 * respects entity save filtering
 *
 * see transform, sprite etc. for example usage
 */
#define entitypool_save_foreach(var, var_s, pool, n, s)                 \
    for (Store *pool##_s__ = NULL;                                      \
         !pool##_s__ && store_child_save(&pool##_s__, n, s); )          \
        entitypool_foreach(var, pool)                                   \
            if (entity_get_save_filter(((EntityPoolElem *) var)->ent))  \
                if (store_child_save(&var_s, NULL, pool##_s__))         \
                    if ((entitypool_elem_save(pool, &var, var_s)), 1)
#define entitypool_load_foreach(var, var_s, pool, n, s)         \
    for (Store *pool##_s__ = NULL;                              \
         !pool##_s__ && store_child_load(&pool##_s__, n, s); )  \
        while (store_child_load(&var_s, NULL, pool##_s__))      \
            if ((entitypool_elem_load(pool, &var, var_s)), 1)

#endif

