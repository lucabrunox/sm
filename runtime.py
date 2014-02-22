# -*- coding: utf8 -*-

from __future__ import print_function
import functools
import io
from collections import deque

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
		
class Runtime:
	def __init__ (self):
		self.eos = EOS ()
		
	def _print (self, *objs):
		objs = map (Lazy.deepresolve, objs)
		print (*objs, end='')
		return objs[0]

	def printS (self, s, *objs):
		s = Lazy.resolve (s)
		while s != self.eos and Lazy.resolve(s[0]) != self.eos:
			print (Lazy.resolve (s[0]), end='')
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
		stream = Stream (s)
		it = ijson.parse (stream)
		def _read():
			try:
				e = it.next()
			except StopIteration:
				return self.eos
				
			return [[e[1], e[2]], Lazy (_read)]
		return _read()
		
	def _list (self, obj, *args):
		obj = Lazy.resolve (obj)
		res = []
		while isinstance (obj, list):
			x,ns = obj
			res.append (Lazy.resolve (x))
			obj = Lazy.resolve (ns)
		return res
