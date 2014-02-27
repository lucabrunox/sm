all: sm hello.s

CSRCS=main.c compile.c objectpool.c code.c parser.c astdumper.c lexer.c
HEADS=compile.h objectpool.h code.h parser.h ast.h lexer.h llvm.h
OBJS=$(subst .c,.o,$(CSRCS)) llvm.o

sm: $(OBJS) Makefile
	g++ -flto -O0 -std=c++11 -g -o sm $(OBJS) -ldl `llvm-config --libs` `llvm-config --ldflags`

%.o: %.c $(HEADS) Makefile
	g++ -O0 -std=c++11 -g -c -o $@ $<

llvm.o: llvm.cpp Makefile
	g++ -O0 -std=c++11 -g -c -o $@ $< `llvm-config --cxxflags`

hello.s: hello.c
	clang -S -emit-llvm hello.c

