#ifndef SM_SCOPE_H
#define SM_SCOPE_H

typedef struct _SmScope SmScope;

SmScope* sm_scope_new (SmScope* parent);
void sm_scope_free (SmScope* scope);

SmScope* sm_scope_get_parent (SmScope* scope);
void sm_scope_set (SmScope* scope, const char* name, int varid);
int sm_scope_get_local_size (SmScope* scope);
int sm_scope_get_size (SmScope* scope);
int sm_scope_lookup (SmScope* scope, const char* name);

#endif