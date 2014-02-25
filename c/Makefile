all: test hello.s

test: main.c code.c code.h parser.c parser.h lexer.c lexer.h llvm.cpp llvm.h Makefile
	g++ -flto -O0 -std=c++11 -g -o test llvm.cpp parser.c lexer.c code.c main.c `llvm-config --cxxflags --ldflags --libs`

hello.s: hello.c Makefile
	clang -S -emit-llvm hello.c
