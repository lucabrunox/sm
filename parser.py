from ast import *
import ttype
import functools
import traceback

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
			'(': self.parse_inner,
			'[': self.parse_list
		}

	def next (self):
		tok = self.lexer.next ()
		self.cur = tok
		return tok

	def accept (self, ex):
		if self.cur.type == ex:
			self.next ()
			return True
		return False
		
	def expect (self, ex):
		if self.cur.type != ex:
			raise RuntimeError ("Expected %s, got %s" % (ex, self.cur))

	def skip (self, ex):
		self.expect (ex)
		self.next ()

	def checkpoint (self):
		return self.lexer.pos, self.cur
		
	def rollback (self, cp):
		self.lexer.pos, self.cur = cp
		
	def parse_id (self):
		self.expect (ttype.ID)
		val = self.cur.value
		self.next ()
		return val

	def parse (self):
		self.next ()
		expr = self.parse_expr ()
		self.expect (ttype.EOF)
		return expr

	def parse_expr (self):
		expr = self.parse_seq ()
		return expr

	def parse_seq (self):
		seq = SeqExpr ()
		expr = self.parse_assign ()
		if not isinstance(expr, AssignExpr):
			return expr
			
		while self.accept (';'):
			seq.assigns.append (expr)
			expr = self.parse_assign ()
			if not isinstance(expr, AssignExpr):
				seq.result = expr
				break

		return seq

	def parse_assign (self):
		begin = self.checkpoint ()
		if self.cur.type == ttype.ID:
			names = [self.parse_id ()]
			while self.accept (','):
				if self.cur.type == ttype.ID:
					names.append (self.parse_id ())
				else:
					self.rollback (begin)
					
			if self.accept ('='):
				expr = self.parse_nonseq ()
				return AssignExpr (names, expr)
			else:
				self.rollback (begin)
	
		return self.parse_nonseq ()

	def parse_nonseq (self):
		return self.parse_func ()
		
	def parse_func (self):
		begin = self.checkpoint ()
		if self.cur.type == ttype.ID:
			params = [self.parse_id ()]
			while self.cur.type == ttype.ID:
				params.append (self.parse_id ())
			if self.accept (':'):
				body = self.parse_func ()
				return FuncExpr (body, params)
			else:
				self.rollback (begin)

		return self.parse_call ()
		
	def parse_call (self):
		func = self.parse_add ()
		begin = self.checkpoint ()
		try:
			arg = self.parse_add ()
			call = CallExpr (func)
			while True:
				call.args.append (arg)
				if self.cur.type in (ttype.EOF, ';', ')'):
					break
				arg = self.parse_add ()
			return call
		except Exception, e:
			self.rollback (begin)
			return func

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
		expr = self.parse_expr ()
		self.skip (')')
		return expr

	def parse_list (self):
		self.skip ('[')
		expr = ListExpr ()
		first = True
		while self.cur.type != ']':
			if not first:
				self.skip (',')
			else:
				first = False
			elem = self.parse_nonseq ()
			expr.elems.append (elem)
		self.skip (']')
		return expr