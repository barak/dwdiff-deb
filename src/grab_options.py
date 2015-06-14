#!/usr/bin/python

import sys

in_parse = False
keeping = False
for line in sys.stdin.readlines():
	if not in_parse:
		if "PARSE_FUNCTION" in line:
			in_parse = True
		continue
	if not keeping:
		if "START_KEEP" in line:
			keeping = True
		if "OPTION" in line or "DOUBLE_DASH" in line or "SINGLE_DASH" in line:
			sys.stdout.write(line)
		elif "#ifdef" in line or "#else" in line or "#endif" in line:
			sys.stdout.write(line)
	else:
		if "STOP_KEEP" in line:
			keeping = False
		else:
			sys.stdout.write(line)
