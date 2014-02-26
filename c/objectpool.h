#ifndef SM_OBJECTPOOL_H
#define SM_OBJECTPOOL_H

typedef struct _SmObjectPool SmObjectPool;

SmObjectPool* sm_object_pool_new (int nobjects, int objectsize);
void* sm_object_pool_acquire (SmObjectPool* pool);
void sm_object_pool_release (SmObjectPool* pool, void* object);
void sm_object_pool_free (SmObjectPool* pool);

#endif