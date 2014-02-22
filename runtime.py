# -*- coding: utf8 -*-

from __future__ import print_function
import collections

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