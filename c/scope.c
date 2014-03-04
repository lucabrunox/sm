#include <glib.h>
#include <stdio.h>
#include <stdint.h>

#include "scope.h"

struct _SmScope {
	SmScope* parent;
	GHashTable* map;
};

SmScope* sm_scope_new (SmScope* parent) {
	SmScope* scope = g_new0 (SmScope, 1);
	scope->parent = parent;
	scope->map = g_hash_table_new (g_str_hash, g_str_equal);
	return scope;
}

void sm_scope_free (SmScope* scope) {
	g_hash_table_unref (scope->map);
	g_free (scope);
}

SmScope* sm_scope_get_parent (SmScope* scope) {
	return scope->parent;
}

void sm_scope_set (SmScope* scope, const char* name, int varid) {
	g_hash_table_insert (scope->map, (gpointer)name, GINT_TO_POINTER(varid));
}

int sm_scope_get_local_size (SmScope* scope) {
	if (!scope) {
		return 0;
	}
	return g_hash_table_size (scope->map);
}

int sm_scope_get_size (SmScope* scope) {
	if (!scope) {
		return 0;
	}
	int size = sm_scope_get_local_size (scope);
	return sm_scope_get_size (scope->parent)+size;
}

int sm_scope_lookup (SmScope* scope, const char* name) {
	while (scope) {
		int64_t id;
		if (g_hash_table_lookup_extended (scope->map, (gpointer)name, NULL, (gpointer*)&id)) {
			return sm_scope_get_size (scope->parent)+id;
		}
		scope = scope->parent;
	}
	return -1;
}
