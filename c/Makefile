all: test hello.s

test: test.cpp Makefile
	g++ -g -o test test.cpp `llvm-config --cxxflags --ldflags --libs`

hello.s: hello.c Makefile
	clang -S -emit-llvm hello.c
