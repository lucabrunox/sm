#!/usr/bin/env python
import gevent.monkey
gevent.monkey.patch_all ()
import os, sys
from lexer import Lexer
from parser import Parser

def main ():
	lexer = Lexer ("<stdin>", sys.stdin.read())
	parser = Parser (lexer)
	ast = parser.parse ()
	print ast

if __name__ == '__main__':
	main ()