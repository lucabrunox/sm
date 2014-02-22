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
		if name in self.vals:
			return self.vals[name]
		if self.parent:
			return self.parent[name]
		raise RuntimeError ("'%s' not found in scope" % name)

	@staticmethod
	def base ():
		runtime = Runtime ()
		scope = Scope ()
		scope.vals = {
			'id': runtime._id,
			'print': runtime._print,
			'eos': runtime.eos,
			'eos?': lambda x: Lazy.resolve(x) == runtime.eos,
			'true': True,
			'false': False
		}
		return scope
		
class Runner:
	def run (self, ast, scope=None):
		self.ret = None
		if not scope:
			self.scope = Scope.base ()
		else:
			self.scope = scope
		ast.accept (self)
		return Lazy.resolve (self.ret)

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
		self.ret = Lazy (lambda: Lazy.resolve(func) (*args))

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

	def visit_if (self, expr):
		expr.cond.accept (self)
		cond = self.ret
		expr.true.accept (self)
		true = self.ret
		expr.false.accept (self)
		false = self.ret

		def _func ():
			if Lazy.resolve (cond):
				return true
			else:
				return false
		self.ret = Lazy (_func)

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

		def _func ():
			if expr.op == '+':
				return Lazy.resolve(left) + Lazy.resolve(right)
			elif expr.op == '-':
				return Lazy.resolve(left) - Lazy.resolve(right)
			elif expr.op == '*':
				return Lazy.resolve(left) * Lazy.resolve(right)
			elif expr.op == '/':
				return Lazy.resolve(left) / Lazy.resolve(right)
		self.ret = Lazy (_func)

	def visit_member (self, expr):
		obj = self.scope
		if expr.inner:
			expr.inner.accept (self)
			obj = self.ret
		self.ret = Lazy (lambda: Lazy.resolve(obj)[expr.name])

	def visit_num_literal (self, expr):
		self.ret = expr.value
