from __future__ import print_function

class Lazy:
	def __init__ (self, func):
		self.func = func

	def resolve (self):
		if not hasattr (self, "resolved"):
			self.resolved = self.func ()
		return self.resolved

class Runtime:
	def resolve (self, obj):
		if isinstance (obj, Lazy):
			return obj.resolve ()
		return obj
		
	def _print (self, *objs):
		objs = map (self.resolve, objs)
		print (*objs)
		return objs[0]

	def _id (self, obj, *args):
		return obj