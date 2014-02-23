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

class SeqExpr:
	def __init__ (self):
		self.assigns = []
		self.result = None

	def accept (self, visitor):
		visitor.visit_seq (self)

	def __str__ (self):
		ret = "("
		for a in self.assigns:
			ret += "%s;\n" % a
		ret += str(self.result)+")"
		return ret

class AssignExpr:
	def __init__ (self, names, inner):
		self.names = names
		self.inner = inner

	def accept (self, visitor):
		visitor.visit_assign (self)
		
	def __str__ (self):
		return "%s = %s" % (", ".join (self.names), self.inner)

class CallExpr:
	def __init__ (self, func):
		self.func = func
		self.args = []

	def accept (self, visitor):
		visitor.visit_call (self)

	def __str__ (self):
		ret = str(self.func)
		for arg in self.args:
			ret += " (%s)" % arg
		return ret

class PipeExpr:
	def __init__ (self, source):
		self.source = source
		self.compose = False
		self.filters = []

	def accept (self, visitor):
		visitor.visit_pipe (self)

	def __str__ (self):
		if self.compose:
			return "(| %s) " % ' | '.join (map (str, self.filters))
		else:
			return "(%s | %s)" % (self.source, ' | '.join (map (str, self.filters)))
		
class FuncExpr:
	def __init__ (self, body, params):
		self.body = body
		self.params = params

	def accept (self, visitor):
		visitor.visit_func (self)

	def __str__ (self):
		return ", ".join (self.params)+": "+str(self.body)

class ListExpr:
	def __init__ (self):
		self.elems = []

	def accept (self, visitor):
		visitor.visit_list (self)

	def __str__ (self):
		return "[%s]" % (", ".join (map (str, self.elems)))

class IfExpr:
	def __init__ (self, cond, true, false):
		self.cond = cond
		self.true = true
		self.false = false

	def accept (self, visitor):
		visitor.visit_if (self)
		
	def __str__ (self):
		return "(if %s then %s else %s)" % (self.cond, self.true, self.false)
		
class BinaryExpr:
	def __init__ (self, op, left, right):
		self.op = op
		self.left = left
		self.right = right

	def accept (self, visitor):
		visitor.visit_binary (self)
		
	def __str__ (self):
		return "(%s %s %s)" % (self.left, self.op, self.right)

class MemberExpr:
	def __init__ (self, inner, name):
		self.inner = inner
		self.name = name

	def accept (self, visitor):
		visitor.visit_member (self)

	def __str__ (self):
		if self.inner:
			return "%s.%s" % (self.inner, self.name)
		else:
			return self.name

class Literal:
	def __init__ (self, value):
		self.value = value

	def accept (self, visitor):
		visitor.visit_literal (self)

	def __str__ (self):
		return str(self.value)