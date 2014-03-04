all: sm hello.s

CSRCS=main.c codegen.c compile.c objectpool.c code.c parser.c astdumper.c lexer.c scope.c
HEADS=codegen.h objectpool.h code.h parser.h ast.h lexer.h llvm.h scope.h compile.h
OBJS=$(subst .c,.o,$(CSRCS)) llvm.o
# CFLAGS=-O2 -g -Wall -Werror -flto 
# LDFLAGS=-flto
CFLAGS=-O0 -g -Wall -Werror
LDFLAGS=

sm: $(OBJS) Makefile
	g++ $(LDFLAGS) -std=c++11 -o sm $(OBJS) -ldl `llvm-config --libs` `llvm-config --ldflags` `pkg-config glib-2.0 --libs`

%.o: %.c $(HEADS) Makefile
	gcc -std=c99 -c -o $@ $< $(CFLAGS) `pkg-config glib-2.0 --cflags` 

llvm.o: llvm.cpp Makefile
	g++ -std=c++11 -Wall -Werror -g -c -o $@ $< $(CFLAGS) `llvm-config --cxxflags`

hello.s: hello.c
	clang -S -emit-llvm hello.c

