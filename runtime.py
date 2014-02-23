# -*- coding: utf8 -*-

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

from __future__ import print_function
import functools
import io, re
from collections import deque

re_type = type(re.compile('foo'))

class Lazy:
	def __init__ (self, func):
		self.func = func

	def __call__ (self):
		if not hasattr (self, "resolved"):
			self.resolved = self.func ()
		return self.resolved

	@staticmethod
	def resolve (obj):
		while isinstance (obj, Lazy):
			obj = obj()
		return obj

	@staticmethod
	def deepresolve (obj):
		obj = Lazy.resolve (obj)
		if isinstance(obj, list):
			for i in range(len(obj)):
				obj[i] = Lazy.deepresolve(obj[i])
		return obj

	@staticmethod
	def deepdiscard (obj):
		q = deque([obj])
		while q:
			obj = Lazy.resolve (q.popleft ())
			if isinstance (obj, list):
				for sub in obj:
					q.append (sub)
		return obj

class EOS:
	def __repr__ (self):
		return "⟂"

class Stream:
	def __init__ (self, sm):
		self.sm = Lazy.resolve (sm)
		self.buffer = ""

	def read (self, count):
		res = self.buffer[:count]
		self.buffer = self.buffer[count:]
		while len(res) < count and isinstance (self.sm, list):
			x,ns = self.sm
			res += Lazy.resolve (x)
			self.sm = Lazy.resolve (ns)
			
		if len(res) > count:
			self.buffer += res[len(res)-count:]
			res = res[:count]
		return res

class Object:
	def __init__ (self, obj):
		self.obj = obj
		
	def __getitem__ (self, name):
		if hasattr(self.obj, '__getitem__'):
			return self.obj[name]
		else:
			return getattr(self.obj, name)

	def __getattr__ (self, name):
		return getattr(self.obj, name)
			
	def __str__ (self):
		return str(self.obj)

	def __repr__ (self):
		return repr(self.obj)
		
class Runtime:
	def __init__ (self):
		self.eos = EOS ()
		
	def _print (self, *objs):
		objs = map (Lazy.deepresolve, objs)
		# import pprint
		# pp = pprint.PrettyPrinter(indent=4)
		# pp.pprint(*objs)
		print (*objs, end='')
		return objs[0]

	def printS (self, s, *objs):
		s = Lazy.resolve (s)
		if s == self.eos:
			return s
		if not isinstance (s, list):
			print (s)
			return s
		while s != self.eos and Lazy.resolve(s[0]) != self.eos:
			print (Lazy.deepresolve (s[0]), end='')
			if len (s) > 1:
				s = Lazy.resolve (s[1])
			else:
				break
		return s

	def stream (self, obj, *args):
		obj = Lazy.resolve (obj)
		if obj == self.eos:
			return self.eos
		if isinstance (obj, list):
			def _func(l):
				if not l:
					return self.eos
				else:
					return [l[0], Lazy(functools.partial (_func, l[1:]))]
			return _func (obj)
		return [obj]

	def read (self, uri, *args):
		h = open (uri, "rb")
		def _read():
			c = h.read (1)
			if not c:
				h.close()
				return self.eos
			return [c, Lazy (_read)]
		return _read()

	def write (self, uri, *args):
		h = open (uri, "wb")
		def _write(c, *args):
			c = Lazy.resolve (c)
			if c == self.eos:
				h.close ()
				return self.eos
			else:
				h.write (c)
				return _write
		return _write
		
	def unique (self, *args):
		return object()

	def string (self, obj, *args):
		obj = Lazy.resolve (obj)
		res = ""
		for x in obj:
			res += x
		return res

	def fromJson (self, s, *args):
		import ijson
		it = ijson.parse (Stream (s))
		def _read():
			try:
				e = it.next()
			except StopIteration:
				return self.eos
				
			return [[e[1], e[2]], Lazy (_read)]
		return _read()

	def parseHtml (self, s, *args):
		import lxml.html
		# no stream reader found :(
		root = lxml.html.parse (Stream (s)).getroot()
		def _read(it):
			try:
				e = it.next()
			except StopIteration:
				return self.eos

			return [[e, Lazy (lambda: _read (iter (e)))], Lazy (lambda: _read(it))]
		return _read (iter (root))

	def fromXml (self, s, *args):
		import libxml2
		input_source = libxml2.inputBuffer(Stream (s))
		reader = input_source.newTextReader("")
		nodetypes = ["none", "element", "attribute", "text", "cdata", "entref", "entity",
					 "procinst", "comment", "document", "doctype", "docfragment", "notation",
					 "whitespace", "preservewhitespace", "endelement", "endentity", "xmldecl"]
		def _read():
			if not reader.Read():
				return self.eos
			return [[reader.Name(), nodetypes[reader.NodeType()]], Lazy (_read)]
		return _read()

	def _not (self, obj, *args):
		obj = Lazy.resolve (obj)
		return self.empty (obj)
		
	def match (self, p, *args):
		def _match (t):
			r = Lazy.resolve(p)
			if isinstance (r, re_type):
				it = r.finditer (Lazy.resolve (t))
			else:
				it = re.compile(re.escape(r)).finditer (Lazy.resolve (t))
				
			def _read():
				try:
					e = it.next()
				except StopIteration:
					return self.eos
				return [Object (e), Lazy (_read)]
			return _read()
		
		# for partial application
		if len(args) > 0:
			t = args[0]
			return _match (t)
		else:
			return _match

	def empty (self, obj, *args):
		obj = Lazy.resolve (obj)
		return not obj or obj == self.eos or obj == [self.eos]

	def _int (self, obj, *args):
		obj = Lazy.resolve (obj)
		return int(obj)

	def _float (self, obj, *args):
		obj = Lazy.resolve (obj)
		return float(obj)

	def _str (self, obj, *args):
		obj = Lazy.resolve (obj)
		return str(obj)

	def _bool (self, obj, *args):
		obj = Lazy.resolve (obj)
		return bool(obj)
		
	def _list (self, obj, *args):
		obj = Lazy.resolve (obj)
		res = []
		while isinstance (obj, list):
			x,ns = obj
			res.append (Lazy.resolve (x))
			obj = Lazy.resolve (ns)
		return res
