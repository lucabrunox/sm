from ast import *
from runtime import *

class Scope:
	def __init__ (self, parent=None):
		self.vals = {}
		self.parent = parent
		
	def __setitem__ (self, name, value):
		if name in self:
			raise RuntimeError ("shadowing '%s'" % name)
		self.vals[name] = value

	def __contains__ (self, name):
		return name in self.vals or (self.parent and name in self.parent)
		
	def __getitem__ (self, name):
		val = self.vals.get (name)
		if val:
			return val
		if self.parent:
			return self.parent[name]
		raise RuntimeError ("'%s' not found in scope" % name)

	@staticmethod
	def base ():
		runtime = Runtime ()
		scope = Scope ()
		scope.vals = {
			'id': runtime._id,
			'print': runtime._print
		}
		return scope
		
class Runner (Visitor):
	def run (self, ast, scope=None):
		self.ret = None
		self.runtime = Runtime ()
		if not scope:
			self.scope = Scope.base ()
		else:
			self.scope = scope
		ast.accept (self)
		return self.ret

	def visit_seq (self, expr):
		for a in expr.assigns:
			a.accept (self)
		expr.result.accept (self)
		
	def visit_assign (self, expr):
		expr.inner.accept (self)
		if len(expr.names) == 1:
			self.scope[expr.names[0]] = self.ret
		else:
			for i in range(len(expr.names)):
				self.scope[expr.names[i]] = self.ret[i]

	def visit_call (self, expr):
		expr.func.accept (self)
		func = self.ret
		args = []
		for arg in expr.args:
			arg.accept (self)
			args.append (self.ret)
		self.ret = func (*args)

	def create_func (self, pscope, body, params):
		def _func (*args):
			scope = Scope (pscope)
			for i in range (min (len (params), len (args))):
				scope[params[i]] = args[i]
				
			if len (args) < len (params):
				# partial application
				return self.create_func (scope, body, params[len(args):])
			else:
				runner = Runner ()
				return runner.run (body, scope)
		return _func
		
	def visit_func (self, expr):
		self.ret = self.create_func (self.scope, expr.body, expr.params)

	def visit_list (self, expr):
		l = []
		for elem in expr.elems:
			elem.accept (self)
			l.append (self.ret)
		self.ret = l
		
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
		obj = self.scope
		if expr.inner:
			expr.inner.accept (self)
			obj = self.ret
		self.ret = obj[expr.name]

	def visit_num_literal (self, expr):
		self.ret = expr.value
