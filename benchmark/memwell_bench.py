#!/usr/bin/env python3

import re
import subprocess
import collections as col

def current_commit():
    '''returns the current git commit; or dies
    '''
    # get current git commit; ergo must run inside repo
    try:
        sub = subprocess.run([ 'git', 'rev-parse', '--short', 'HEAD' ],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            shell=False, check=True);
    except subprocess.CalledProcessError as err:
        print(err.cmd)
        print(err.stdout.decode('ascii'))
        print(err.stderr.decode('ascii'))
        exit(1)

    return sub.stdout.decode('ascii').strip()

rex = re.compile('(([0-9]+)\/([0-9]+))\s(\S+)\s([0-9]+)\s+OK', re.MULTILINE)
rex_timeout = re.compile('(([0-9]+)\/([0-9]+))\s(\S+)\s([0-9]+)\s+TIMEOUT', re.MULTILINE)
rex1 = re.compile('^(cpu time\s[0-9]+\.[0-9]+)s;\s(wall time [0-9]+\.[0-9]+)s', re.MULTILINE)
rex_ops = re.compile('operations\s([0-9]+)')


def run_meson_configure(option_value):
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

rex_option = re.compile('.*threads ([0-9]+)', re.DOTALL)

def read_threads_option():
    try:
	    sub = subprocess.run(['meson', 'configure'],
		                     stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             shell=False, check=True)
    except subprocess.CalledProcessError as err:
               print(err.cmd)
               print(err.stdout.decode('ascii'))
               print(err.stderr.decode('ascii'))
               exit(1)
	
    print(sub.stdout.decode('ascii'))
    m = rex_option.match(sub.stdout.decode('ascii'))
    if m:
       return int(m.group(1))
    else:
       print('no match')

def run_benchmark():
	# Can only be run from the build dir
	# Check if build dir???
    try:
	    sub = subprocess.run(['ninja benchmark'],
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

	#res1 = rex1.findall(data)
	res = rex.findall(data)
	#res_timeout = rex_timeout.findall(data)
	res_ops = rex_ops.findall(data)
	
	d = col.OrderedDict()
	# handle any rejected 'TIMEOUT' values by inserting 0 values 
	# in those places
#	if res_timeout and res1:
#		fake_cpu = 'cpu time 0.0'
#		fake_wall = 'wall time 0.0'
#		tup = tuple([fake_cpu, fake_wall])
#		for a in res_timeout:
#			d[a] = tup 
	
	if res and res_ops:
		for (a,b) in zip(res,res_ops):
			d[a] = [ b ]
	else:
		print('TEST: no regex match!')
	
	return d

import platform
import multiprocessing as mp

def get_system_info():
	return 'OS {0} - CPU Cores {1} - Architecture {2}'.format(platform.system(),mp.cpu_count(),platform.processor())

dd = col.OrderedDict()
import statistics as st
threads = col.OrderedDict()

#threads and the avgs_* dicts are both indexed by the benchmark run id 

#threads and the avgs_* dicts are both indexed by the benchmark run id 

def  summate_runs(runs):
	for k,v in runs.items():
		for s in v:
			# s[1] is the number of the benchmark run out of X runs
			threads[int(s[1])] = int(s[4])
		for k,v in v.items():
			#key is a tuple of benchmark id + process name
			key = tuple([threads[int(k[1])], k[3]])
			if k[1] not in dd.keys():
				dd[key] = [( k, v )]
			else:
				dd[key].append((k, v))

	avgs_ops = col.OrderedDict()
	key = ()
	for k,v in dd.items():
		for line in v:
			#print(v[0][0][3])
			for l in line:
				if len(l) >= 3:
					# key is number of threads, process name
					key = tuple([k[0], l[3]])
				else:
					if key in avgs_ops.keys():
						avgs_ops[key].append(int(l[0]))
					else:
						avgs_ops[key] = [ int(l[0]) ]
	avgs_ops1 = col.OrderedDict({ k : st.mean(v) for k,v in avgs_ops.items() })
#	print(avgs_ops)	
	return avgs_ops1
#(('1/48', '1', '48', 'WELL_DO_XCH-BOUNDED;', '1', '1'), ('cpu time 0.6633', 'wall time 0.3321'))

# Do not use Xwindows;
#+  needs to be called before pyplot is imported
import matplotlib 
matplotlib.use('Agg')
import matplotlib.pyplot as plt 

import math

def make_plots(cpu_time, chart_suffix, y_label_name, seconds = 5):
	#make a list of all 'chart_name' charts
	charts = col.OrderedDict()
	charts['XCH'] = []
	charts['MTX'] = []
	charts['SPL'] = []
	charts['CAS'] = []
	for k,v in cpu_time.items():
		found = k[1].find(chart_suffix)
		if found > 0:
			if 'WELL_XCH' in k[1]:
				charts['XCH'].append(tuple([k[0],v]))
			if 'WELL_MTX' in k[1]:
				charts['MTX'].append(tuple([k[0],v]))
			if 'WELL_SPL' in k[1]:
				charts['SPL'].append(tuple([k[0],v]))	
			if 'WELL_CAS' in k[1]:
				charts['CAS'].append(tuple([k[0],v]))
	
	#for each chart (mtx, xch and spl) and I need a line on the plot
	lin_y_mtx = col.OrderedDict()
	lin_y_xch = col.OrderedDict()
	lin_y_spl = col.OrderedDict()
	lin_y_cas = col.OrderedDict()
	for k,v in charts.items():
		for l in v:
			# make a y axis for each chart
			if k == 'MTX':
				lin_y_mtx[l[0]] = l[1]
			if k == 'XCH':
				lin_y_xch[l[0]] = l[1]
			if k == 'SPL':
				lin_y_spl[l[0]] = l[1]
			if k == 'CAS':
				lin_y_cas[l[0]] = l[1]
	
	commit_id = current_commit()
	lin_x_ticks = [ 0, 2, 4, 8, 16, 32 ]
	lin_x = [ 0, 1, 2, 4, 8, 16 ]
	lin_x_log = [ int(math.log2(x)) for x in lin_x[1:] ]
	lin_x_log.insert(0,-1)
	_, ax = plt.subplots(figsize=(13, 8))
	ax.set_xlabel('threads')
	ax.set_xticks(lin_x_log, minor=True)
	ax.set_xticklabels(lin_x_ticks)
	all_values = []
	all_values.extend(lin_y_mtx.values())
	all_values.extend(lin_y_xch.values())
	all_values.extend(lin_y_spl.values())
	all_values.extend(lin_y_cas.values())
	all_values.sort()
	powers_of_ten = [ pow(10,i) for i in range(1, 9) ]
	y_min = min(powers_of_ten)
	y_max = max(powers_of_ten)
	ax.set_ylabel('operations per second')
	# make sure the arrays are sorted by the thread count (keys)
	lin_y_mtx_sorted = col.OrderedDict(sorted(lin_y_mtx.items(), key=lambda item: item))
	lin_y_xch_sorted = col.OrderedDict(sorted(lin_y_xch.items(), key=lambda item: item))
	lin_y_spl_sorted = col.OrderedDict(sorted(lin_y_spl.items(), key=lambda item: item))
	lin_y_cas_sorted = col.OrderedDict(sorted(lin_y_cas.items(), key=lambda item: item))

	x = lin_x_log
	y1 = [ (i/seconds) for i in lin_y_mtx_sorted.values() ]
	y2 = [ (i/seconds) for i in lin_y_xch_sorted.values() ]
	y3 = [ (i/seconds) for i in lin_y_spl_sorted.values() ]
	y4 = [ (i/seconds) for i in lin_y_cas_sorted.values() ]

	plt.plot(x,y1, 'b-', x, y2, 'g-', x, y3, 'r-', x,y4, 'y-')
	plt.yscale(value='log', basey=10, subsy=[1,2,3,4,5,6,7,8,9])
	plt.axis(xmin = min(lin_x_log), xmax = max(lin_x_log), ymin = y_min, ymax = y_max)
	plt.legend(['MTX', 'XCH', 'SPL', 'CAS'], loc='upper right')
	plt.title('MemoryWell {0}: transactions through single buffer;\
			\r\nfail strategy {1}'.format(commit_id, chart_suffix[1:]))

	ax.text(x=0.02, y=0, va='bottom', ha='left', s='{0}'.format(get_system_info()), transform=ax.transAxes)	
	plt.grid(b=True, which='major')
	plt.grid(b=True, which='minor', linestyle='--')
	plt.savefig('{0} - {1} {2}.pdf'.format(commit_id, y_label_name, chart_suffix),
			dpi=600, papertype='a4', orientation='landscape', bbbox_inches='tight',pad_inches=0)

 
import sys, getopt

def  human_format(num):
    '''formats 'num' into the proper K|M|G|TiB. 
	returns a string.
    '''
    magnitude = 0
    while abs(num) >= 1000:
        magnitude += 1
        num /= 1000.0
    return '%.0f %s' % (num, [ '', 'K', 'M', 'G', 'T', 'P'][magnitude])

def print_usage():
	print('./memwell_bench -r <number of benchmark iterations>')

def parse_opts(opts):
	iterations = 5
	for o, arg in opts:
		if o in ('-r', '--runs'):
			iterations = int(arg)
		elif o in ('-h', '--help'):
			print_usage()
			sys.exit(0)
		else:
			print('no such option')
	
	return iterations

def main():
	filename = 'meson-logs/benchmarklog.txt'
	runs = col.OrderedDict()
	
# parse options
	try:
		opts, args = getopt.getopt(sys.argv[1:], "r:h", ["runs=","help"])
	except getopt.GetoptError as err:
		print(err)
		print_usage()
		sys.exit(1)
	
	iterations = parse_opts(opts)

	print('running {0} iterations'.format(iterations))

	for i in range(0, iterations):
		print(i)
		run_benchmark()
		runs[i] = parse_file(filename)
	
	avgs_ops = summate_runs(runs)
	
	make_plots(avgs_ops, '_BOUNDED', 'operations')
	make_plots(avgs_ops, '_YIELD', 'operations')
	make_plots(avgs_ops, '_SPIN', 'operations')
	make_plots(avgs_ops, '_SLEEP', 'operations')
if __name__ == "__main__":
	main()
