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
			c = self.peek()
			self.read()

		if c.isalpha():
			id = c
			while self.peek().isalnum():
				id += self.read ()
			if self.peek() == '?':
				id += self.read ()
			return Token (ttype.ID, id)

		if c == '_':
			return Token (ttype.ID, '_')

		if c.isdigit():
			num = int(c)
			while self.peek().isdigit():
				num *= 10
				num += int(self.read())
			return Token (ttype.NUM, num)

		if c == '!':
			assert self.read() == '='
			return Token('!=')
			
		if c in "+-*/<>=()[]{},;:|":
			if c in '=*/' and self.peek() == c:
				c += self.read()
			elif c in '<>' and self.peek() == '=':
				c += self.read()
			return Token (c)

		if c in "'\"":
			q = c
			s = ""
			while self.peek() != q:
				if self.peek() == '\\':
					self.read()
					n = self.read ()
					if n == 'n':
						s += '\n'
					else:
						s += n
				else:
					s += self.read()
			self.read()
			return Token (ttype.STR, s)
			
		return Token (ttype.UNKNOWN)