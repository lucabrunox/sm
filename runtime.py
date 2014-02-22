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
		
class Runtime:
	def __init__ (self):
		self.eos = EOS ()
		
	def _print (self, *objs):
		objs = map (Lazy.deepresolve, objs)
		print (*objs, end='')
		return objs[0]

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
		def _func():
			h = open (uri, "rb")
			def _read():
				c = h.read (1)
				if not c:
					return self.eos
				return [c, Lazy (_read)]
			return _read()

		return Lazy (_func)
