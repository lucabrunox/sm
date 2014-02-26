all: test hello.s

test: main.c objectpool.c objectpool.h code.c code.h parser.c parser.h astdumper.c ast.h lexer.c lexer.h llvm.cpp llvm.h Makefile
	g++ -flto -O0 -std=c++11 -g -o test objectpool.c llvm.cpp parser.c astdumper.c lexer.c code.c main.c `llvm-config --cxxflags --ldflags --libs`

hello.s: hello.c Makefile
	clang -S -emit-llvm hello.c
