#!/usr/bin/env python
import gevent.monkey
gevent.monkey.patch_all ()
import os, sys
from lexer import Lexer
from parser import Parser
from runner import Runner

def main ():
	lexer = Lexer ("<stdin>", sys.stdin.read())
	parser = Parser (lexer)
	ast = parser.parse ()
	runner = Runner ()
	result = runner.run (ast)

if __name__ == '__main__':
	main ()