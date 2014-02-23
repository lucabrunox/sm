tnames = [
	'EOF',
	'UNKNOWN',
	'ID',
	'NUM',
	'STR',
	'REGEX'
]
for tname in tnames:
	locals()[tname] = tname
