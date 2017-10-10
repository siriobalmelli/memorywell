#!/usr/bin/env python3

import re
import subprocess
import collections as col

rex = re.compile('(([0-9]+)\/([0-9]+))\s(\S+)\s([0-9]+)->([0-9]+)', re.MULTILINE)
rex1 = re.compile('^(cpu time\s[0-9]+\.[0-9]+)s;\s(wall time [0-9]+\.[0-9]+)s', re.MULTILINE)

def run_meson(option_value):
	# run meson configure 
    try:
	    subprocess.run(['meson', 'configure', '-Dthreads={}'.format(option_value)],
		                     stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             shell=False, check=True)
    except subprocess.CalledProcessError as err:
               print(err.cmd)
               print(err.stdout.decode('ascii'))
               print(err.stderr.decode('ascii'))
               exit(1)
	
def run_benchmark():
	# Can only be run from the build dir
	# Check if build dir???
    try:
	    sub = subprocess.run(['ninja benchmark', 'benchmark'],
		                     stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             shell=True, check=True)
	    #print(sub.stdout.decode('ascii'))
	    output = sub.stdout.decode('ascii')
	    print(output)
    except subprocess.CalledProcessError as err:
               print(err.cmd)
               print(err.stdout.decode('ascii'))
               print(err.stderr.decode('ascii'))
               exit(1)

def parse_file(filename):
	with open(filename) as f:
		data = f.read()

	res1 = rex1.findall(data)
	res = rex.findall(data)
	d = col.OrderedDict()
	if res and res1:
		for (a,b) in zip(res,res1):
			print(a)
			print(b)
			d[a] = b
	else:
		print('no regex match!')
	
	return d

def summate_runs(runs):
	for k,v in runs.items():
		for k,v in v.items():
			print(k)
			print(v)

def main():
	filename = 'meson-logs/benchmarklog.txt'
	#filename = 'foo.txt'
	runs = col.OrderedDict()
	for i in range(0, 3):
		run_benchmark()
		runs[i] = parse_file(filename)
	
	summate_runs(runs)
if __name__ == "__main__":
	main()
