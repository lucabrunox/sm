tnames = [
	'EOF',
	'UNKNOWN',
	'ID',
	'NUM',
	'STR'
]
for tname in tnames:
	locals()[tname] = tname
