from ast import *

class Scope:
	def __init__ (self, parent=None):
		self.vals = {}
		self.parent = parent
		
	def set (self, name, value):
		self.vals[name] = value

	def get (self, name):
		val = self.vals.get (name)
		if val:
			return val
		if self.parent:
			return self.parent.get (name)
		raise RuntimeError ("'%s' not found in scope" % name)

class Runner (Visitor):
	def run (self, ast):
		self.ret = None
		self.scope = Scope ()
		ast.accept (self)
		return self.ret

	def visit_seq (self, expr):
		for a in expr.assigns:
			a.accept (self)
		expr.result.accept (self)
		
	def visit_assign (self, expr):
		expr.inner.accept (self)
		self.scope.set (expr.name, self.ret)
		
	def visit_binary (self, expr):
		expr.left.accept (self)
		left = self.ret
		expr.right.accept (self)
		right = self.ret
		
		if expr.op == '+':
			self.ret = left + right
		elif expr.op == '-':
			self.ret = left - right
		elif expr.op == '*':
			self.ret = left * right
		elif expr.op == '/':
			self.ret = left / right

	def visit_member (self, expr):
		self.ret = self.scope.get (expr.name)

	def visit_num_literal (self, expr):
		self.ret = expr.value

