# This file is part of SM (Stream Manipulator).
#
# SM is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# SM is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with SM.  If not, see <http://www.gnu.org/licenses/>.

from ast import *
from runtime import *
import functools

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
	def base (runtime):
		scope = Scope ()
		scope.vals = {
			'print': runtime._print,
			'printS': runtime.printS,
			'eos': runtime.eos,
			'eos?': lambda x: Lazy.resolve(x) == runtime.eos,
			'true': True,
			'false': False,
			'stream': runtime.stream,
			'read': runtime.read,
			'write': runtime.write,
			'unique': runtime.unique,
			'string': runtime.string,
			'list': runtime._list,
			'fromJson': runtime.fromJson,
			'parseHtml': runtime.parseHtml,
			'fromXml': runtime.fromXml,
			'match': runtime.match,
			'empty?': runtime.empty,
			'int': runtime._int,
			'float': runtime._float,
			'str': runtime._str,
			'not': runtime._not,
			'bool': runtime._bool
		}
		return scope
		
class Runner:
	def run (self, ast, scope):
		self.ret = None
		self.scope = scope
		ast.accept (self)
		return Lazy.resolve (self.ret)

	def visit_seq (self, expr):
		oldscope = self.scope
		self.scope = Scope (oldscope)

		for a in expr.assigns:
			a.accept (self)
		expr.result.accept (self)
		
		self.scope = oldscope
		
	def visit_assign (self, expr):
		expr.inner.accept (self)
		if len(expr.names) == 1:
			if expr.names[0] != '_':
				# TODO: report unused expression
				self.scope[expr.names[0]] = self.ret
		else:
			l = self.ret
			for i in range(len(expr.names)):
				def _func(j):
					r = Lazy.resolve(l)
					if r == self.scope['eos']:
						return self.scope['eos']
					if not isinstance(r, list):
						raise RuntimeError ("cannot unpack: %s at %s: " % (r, expr))
					if j < len(r):
						return r[j]
					else:
						return self.scope['eos']
				if expr.names[i] != '_':
					self.scope[expr.names[i]] = Lazy(functools.partial (_func, i))

	def visit_call (self, expr):
		expr.func.accept (self)
		func = self.ret
		args = []
		for arg in expr.args:
			arg.accept (self)
			args.append (self.ret)
		self.ret = Lazy (lambda: Lazy.resolve(func) (*args))

	def visit_pipe (self, expr):
		expr.source.accept (self)
		for f in expr.filters:
			source = self.ret
			f.accept (self)
			func = self.ret
			def _func(filt, sourc):
				return Lazy (lambda: Lazy.resolve(filt) (sourc))
			self.ret = _func (func, source)
		
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
				val = runner.run (body, scope)
				if len (args) > len (params):
					# call returned function
					val = Lazy.resolve (val)
					return val (*args[len(params):])
				else:
					return val
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
			if not self.scope['empty?'] (Lazy.resolve (cond)):
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
			l = Lazy.resolve (left)
			if expr.op == 'or':
				if not self.scope['empty?'] (l):
					return True
				r = Lazy.resolve (right)
				return not not r
			elif expr.op == 'and':
				if self.scope['empty?'] (l):
					return False
				r = Lazy.resolve(right)
				return not not r

			r = Lazy.resolve (right)
			if expr.op == '+':
				return l + r
			elif expr.op == '-':
				return l - r
			elif expr.op == '*':
				return l * r
			elif expr.op == '/':
				return l / r
			elif expr.op == '<':
				return l < r
			elif expr.op == '>':
				return l > r
			elif expr.op == '!=':
				return l != r
			elif expr.op == '==':
				return l == r
			elif expr.op == '<=':
				return l <= r
			elif expr.op == '>=':
				return l >= r
			else:
				assert False
		self.ret = Lazy (_func)

	def visit_member (self, expr):
		obj = self.scope
		if expr.inner:
			expr.inner.accept (self)
			obj = self.ret
		self.ret = Lazy (lambda: Lazy.resolve(obj)[expr.name])

	def visit_literal (self, expr):
		self.ret = expr.value
