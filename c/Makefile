all: test hello.s

test: main.c code.c code.h llvm.cpp llvm.h Makefile
	g++ -flto -O0 -g -o test llvm.cpp code.c main.c `llvm-config --cxxflags --ldflags --libs`

hello.s: hello.c Makefile
	clang -S -emit-llvm hello.c
