from ast import *
import ttype
import functools

class Position:
	def __init__ (self, row, col):
		self.row = row
		self.col = col

class Location:
	def __init__ (self, name, start, end):
		self.name = name
		self.start = start
		self.end = end

class Parser:
	def __init__ (self, lexer):
		self.lexer = lexer
		self.cur = None
		self.primary_map = {
			ttype.ID: self.parse_member,
			ttype.NUM: self.parse_num_literal,
			'(': self.parse_inner
		}

	def pos (self):
		return Position (self.lexer.row, self.lexer.col)
		
	def next (self):
		tok = self.lexer.next ()
		self.cur = tok
		return tok

	def expect (self, ex):
		if self.cur.type != ex:
			raise RuntimeError ("Expected %s, got %s" % (ex, self.cur))

	def skip (self, ex):
		self.expect (ex)
		self.next ()
		
	def rollback (self, pos):
		self.lexer.pos = pos
		
	def parse (self):
		self.next ()
		expr = self.parse_add ()
		self.expect (ttype.EOF)
		return expr

	def parse_id (self):
		self.expect (ttype.ID)
		val = self.cur.value
		self.next ()
		return val
		
	def parse_add (self):
		left = self.parse_mul ()
		if self.cur.type in "+-":
			op = self.cur.type
			self.next ()
			right = self.parse_add ()
			return BinaryExpr (op, left, right)
		return left

	def parse_mul (self):
		left = self.parse_primary ()
		if self.cur.type in "*/":
			op = self.cur.type
			self.next ()
			right = self.parse_mul ()
			return BinaryExpr (op, left, right)
		return left

	def parse_primary (self):
		func = self.primary_map.get (self.cur.type)
		if not func:
			raise RuntimeError ("Unexpected %s" % self.cur)
		expr = func ()
		return expr

	def parse_member (self, inner=None):
		id = self.parse_id ()
		return MemberExpr (inner, id)

	def parse_num_literal (self):
		self.expect (ttype.NUM)
		expr = NumLiteral (self.cur.value)
		self.next ()
		return expr
		
	def parse_inner (self):
		self.skip ('(')
		expr = self.parse ()
		self.skip (')')
		return expr