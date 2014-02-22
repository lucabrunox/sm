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
			ttype.NUM: functools.partial (self.parse_literal, ttype.NUM),
			ttype.STR: functools.partial (self.parse_literal, ttype.STR),
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

	def accept_id (self, ex):
		if self.cur.type == ttype.ID and self.cur.value == ex:
			self.next ()
			return True
		return False
		
	def expect (self, ex):
		if self.cur.type != ex:
			raise RuntimeError ("Expected %s, got %s at %d,%d" % (ex, self.cur, self.lexer.row, self.lexer.col))

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
				expr = self.parse_func (True)
				return AssignExpr (names, expr)
			else:
				self.rollback (begin)
	
		return self.parse_func (True)

	def parse_func (self, seq):
		begin = self.checkpoint ()
		if self.cur.type == ttype.ID:
			params = [self.parse_id ()]
			while self.cur.type == ttype.ID:
				params.append (self.parse_id ())
			if self.accept (':'):
				if seq:
					body = self.parse_seq ()
				else:
					body = self.parse_if ()
				return FuncExpr (body, params)
			else:
				self.rollback (begin)

		return self.parse_if ()

	def parse_if (self):
		begin = self.checkpoint ()
		if self.accept_id ('if'):
			cond = self.parse_eq ()
			if self.accept_id ('then'):
				true = self.parse_eq ()
				if self.accept_id ('else'):
					false = self.parse_if ()
					return IfExpr (cond, true, false)
	
		self.rollback (begin)
		return self.parse_eq ()

	def parse_eq (self):
		left = self.parse_rel ()
		if self.cur.type in ["==", "!="]:
			op = self.cur.type
			self.next ()
			right = self.parse_eq ()
			return BinaryExpr (op, left, right)
		return left

	def parse_rel (self):
		left = self.parse_add ()
		if self.cur.type in "<>":
			op = self.cur.type
			self.next ()
			right = self.parse_rel ()
			return BinaryExpr (op, left, right)
		return left
		
	def parse_add (self):
		left = self.parse_mul ()
		if self.cur.type in "+-":
			op = self.cur.type
			self.next ()
			right = self.parse_add ()
			return BinaryExpr (op, left, right)
		return left

	def parse_mul (self):
		left = self.parse_pipe ()
		if self.cur.type in "*/":
			op = self.cur.type
			self.next ()
			right = self.parse_mul ()
			return BinaryExpr (op, left, right)
		return left

	def parse_pipe (self):
		left = self.parse_call ()
		if self.accept ('|'):
			right = self.parse_pipe ()
			call = CallExpr (right)
			call.args = [left]
			return call
		return left

	def parse_call (self):
		func = self.parse_primary ()
		begin = self.checkpoint ()
		try:
			call = CallExpr (func)
			while self.cur.type not in (ttype.EOF, ';', ')', ',', ']') and not (self.cur.type == ttype.ID and self.cur.value in ('then', 'else')):
				arg = self.parse_primary ()
				call.args.append (arg)
			if call.args:
				return call
			else:
				return func
		except Exception, e:
			# traceback.print_exc ()
			self.rollback (begin)
			return func
			
	def parse_primary (self):
		func = self.primary_map.get (self.cur.type)
		if not func:
			raise RuntimeError ("Unexpected %s" % self.cur)
		expr = func ()
		return expr

		
	def parse_member (self, inner=None):
		id = self.parse_id ()
		return MemberExpr (inner, id)

	def parse_literal (self, t):
		self.expect (t)
		expr = Literal (self.cur.value)
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
			elem = self.parse_func (False)
			expr.elems.append (elem)
		self.skip (']')
		return expr