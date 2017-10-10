#!/usr/bin/env python3

import re

rex = re.compile('^([0-9][0-9]\/[0-9][0-9]) (WELL_DO_[A-Z]*-[A-Z]*); ([0-9])->([0-9])')

def parse_file(filename):
	with open(filename) as f:
		data = f.read()
	
	print(data)
			
	res = rex.findall(data)
	if res:
		for a in res:
			print(a)
	else:
		print('no regex match!')
	

def main():
	#filename = 'meson-logs/benchmarklog.txt'
	filename = 'foo.txt'
	parse_file(filename)


if __name__ == "__main__":
	main()
