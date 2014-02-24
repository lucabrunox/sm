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

		while c == '#':
			# comment
			while self.peek() != '\n':
				self.read()
			self.skip_spaces ()
			c = self.read()

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

		if c in "+-*/<>=()[]{}.,;:|":
			if c in '=*/' and self.peek() == c:
				c += self.read()
			elif c in '<>' and self.peek() == '=':
				c += self.read()
			return Token (c)
			
		if c in "'\"~`":
			q = c
			s = ""
			while self.peek() != q:
				if self.peek() == '\\':
					self.read()
					n = self.read ()
					if q not in ('~`') and n == 'n':
						s += '\n'
					else:
						s += n
				else:
					s += self.read()
			self.read()
			if q == '~':
				import re
				return Token (ttype.REGEX, re.compile(s))
			elif q == '`':
				return Token (ttype.SHELL, s)
			else:
				return Token (ttype.STR, s)
			
		return Token (ttype.UNKNOWN)