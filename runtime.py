from __future__ import print_function

class Lazy:
	def __init__ (self, func):
		self.func = func

	def resolve (self):
		return self.func ()

class Runtime:
	def resolve (self, obj):
		if isinstance (obj, Lazy):
			return obj.resolve ()
		return obj
		
	def _print (self, *objs):
		objs = map (self.resolve, objs)
		print (*objs)
		return objs[0]