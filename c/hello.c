#include <stdlib.h>

struct thunk {
	int foo;
};

void asd (int opcode) {
	static const void *codetable[] =
    { &&RETURN };

  goto *codetable[opcode];
RETURN:
	printf("%p", codetable[0]);
  return;
}

void main () {
	asd(0);
}
