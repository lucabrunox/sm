#include <stdlib.h>

struct thunk {
	int foo;
	struct thunk** scopes[];
};

void main () {
	struct thunk* asd = NULL;
	struct thunk* bar = asd[0].scopes[0][0];
	bar->foo = 123;
}
