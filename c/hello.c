#include <stdlib.h>

struct thunk {
	int foo;
};

void main () {
	struct thunk* foo = malloc(sizeof(struct thunk));
}
