#include <stdlib.h>
#include <glib.h>

#include "objectpool.h"

struct _SmObjectPool {
	int nobjects;
	int objectsize;

	int curobjects;
	int cursize;
	void** objects;
};

SmObjectPool* sm_object_pool_new (int nobjects, int objectsize) {
	SmObjectPool* pool = g_new0 (SmObjectPool, 1);
	pool->nobjects = nobjects;
	pool->objectsize = objectsize;
	pool->cursize = 16;
	pool->objects = (void**)malloc(sizeof(void*)*pool->cursize);
	return pool;
}

void sm_object_pool_free (SmObjectPool* pool) {
	for (int i=0; i < pool->curobjects; i++) {
		free (pool->objects[i]);
	}
	free (pool->objects);
	g_free (pool);
}

void* sm_object_pool_acquire (SmObjectPool* pool) {
	if (pool->curobjects <= 0) {
		return malloc(pool->objectsize);
	} else {
		return pool->objects[--pool->curobjects];
	}
}

void sm_object_pool_release (SmObjectPool* pool, void* object) {
	if (pool->cursize >= pool->nobjects) {
		// pool is full
		free(object);
	} else {
		pool->curobjects++;
		if (pool->curobjects >= pool->cursize) {
			pool->cursize = pool->cursize*2+1;
			pool->objects = (void**)realloc(pool->objects, sizeof(void*)*pool->cursize);
		}
		pool->objects[pool->curobjects-1] = object;
	}
}
