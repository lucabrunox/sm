class BinaryExpr:
	def __init__ (self, op, left, right):
		self.op = op
		self.left = left
		self.right = right

	def __str__ (self):
		return "(%s %s %s)" % (self.left, self.op, self.right)

class MemberExpr:
	def __init__ (self, inner, name):
		self.inner = inner
		self.name = name

	def __str__ (self):
		if self.inner:
			return "%s.%s" % (self.inner, self.name)
		else:
			return self.name

class NumLiteral:
	def __init__ (self, num):
		self.num = num

	def __str__ (self):
		return str(self.num)