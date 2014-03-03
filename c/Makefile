all: sm hello.s

CSRCS=main.c compile.c objectpool.c code.c parser.c astdumper.c lexer.c
HEADS=compile.h objectpool.h code.h parser.h ast.h lexer.h llvm.h
OBJS=$(subst .c,.o,$(CSRCS)) llvm.o

sm: $(OBJS) Makefile
	g++ -flto -O2 -std=c++11 -g -o sm $(OBJS) -ldl `llvm-config --libs` `llvm-config --ldflags` `pkg-config glib-2.0 --libs`

%.o: %.c $(HEADS) Makefile
	gcc -O2 -std=c99 -Wall -Werror -g -c -o $@ $< `pkg-config glib-2.0 --cflags`

llvm.o: llvm.cpp Makefile
	g++ -O2 -std=c++11 -Wall -Werror -g -c -o $@ $< `llvm-config --cxxflags`

hello.s: hello.c
	clang -S -emit-llvm hello.c

