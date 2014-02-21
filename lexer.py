import ttype
import io

class Token:
	def __init__ (self, t, value=None):
		self.type = t
		self.value = value

	def __repr__ (self):
		return "%s(%s)" % (self.type, self.value)

class Lexer:
	def __init__ (self, name, text):
		self.name = name
		self.text = text
		self.len = len(text)
		self.pos = 0
		self.row = 0
		self.col = 0

	def peek (self):
		if self.pos >= self.len:
			return ''
		return self.text[self.pos]

	def read (self):
		c = self.peek ()
		if c == '\n':
			self.row += 1
			self.col = 0
		else:
			self.col += 1
		self.pos += 1
		return c
		
	def skip_spaces (self):
		while self.peek().isspace():
			self.read()
		
	def next (self):
		self.skip_spaces ()
		c = self.read()
		if not c:
			return Token (ttype.EOF)

		if c == '#':
			# comment
			while self.read() != '\n':
				continue

		if c.isalpha():
			id = c
			while self.peek().isalnum():
				id += self.read ()
			return Token (ttype.ID, id)

		if c.isdigit():
			num = int(c)
			while self.peek().isdigit():
				num *= 10
				num += int(self.read())
			return Token (ttype.NUM, num)

		if c in "+-*/=()[]{};:|":
			return Token (c)

		return Token (ttype.UNKNOWN)