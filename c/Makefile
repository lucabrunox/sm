all: test hello.s

test: main.c llvm.cpp Makefile
	g++ -g -o test llvm.cpp main.c `llvm-config --cxxflags --ldflags --libs`

hello.s: hello.c Makefile
	clang -S -emit-llvm hello.c
