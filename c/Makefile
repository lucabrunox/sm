all: test hello.s

test: main.c code.c llvm.cpp Makefile
	g++ -flto -g -o test llvm.cpp code.c main.c `llvm-config --cxxflags --ldflags --libs`

hello.s: hello.c Makefile
	clang -S -emit-llvm hello.c
