class Visitor:
	def visit_binary (self, expr):
		pass

	def visit_member (self, expr):
		pass

	def visit_num_literal (self, expr):
		pass

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
			ret += " "+str(arg)
		return ret

class FuncExpr:
	def __init__ (self, body, params):
		self.body = body
		self.params = params

	def accept (self, visitor):
		visitor.visit_func (self)

	def __str__ (self):
		return ", ".join (params)+": "+str(self.body)

class ListExpr:
	def __init__ (self):
		self.elems = []

	def accept (self, visitor):
		visitor.visit_list (self)

	def __str__ (self):
		return "[%s]" % (", ".join (map (str, self.elems)))

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

class NumLiteral:
	def __init__ (self, value):
		self.value = value

	def accept (self, visitor):
		visitor.visit_num_literal (self)

	def __str__ (self):
		return str(self.value)