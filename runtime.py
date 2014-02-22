# -*- coding: utf8 -*-

from __future__ import print_function
import collections
import functools

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

class EOS:
	def __repr__ (self):
		return "⟂"
		
class Runtime:
	def __init__ (self):
		self.eos = EOS ()
		
	def _print (self, *objs):
		objs = map (Lazy.deepresolve, objs)
		print (*objs)
		return objs[0]

	def _id (self, obj, *args):
		return obj

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
