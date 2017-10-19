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

rex = re.compile('(([0-9]+)\/([0-9]+))\s(\S+)\s([0-9]+)->([0-9]+)\s+OK', re.MULTILINE)
rex1 = re.compile('^(cpu time\s[0-9]+\.[0-9]+)s;\s(wall time [0-9]+\.[0-9]+)s', re.MULTILINE)

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

	res1 = rex1.findall(data)
	res = rex.findall(data)
	d = col.OrderedDict()
	if res and res1:
		for (a,b) in zip(res,res1):
			d[a] = b
	else:
		print('no regex match!')

	return d

dd = col.OrderedDict()
import statistics as st
threads = col.OrderedDict()

#threads and the avgs_* dicts are both indexed by the benchmark run id 

def summate_runs(runs):
	for k,v in runs.items():
		for s in v:
			# s[1] is the number of the benchmark run out of X runs
			threads[int(s[1])] = int(s[4]) + int(s[5])
		for k,v in v.items():
			#key is a tuple of benchmark id + process name
			key = tuple([threads[int(k[1])], k[3]])
			if k[1] not in dd.keys():
				dd[key] = [( k, v )]
			else:
				dd[key].append((k, v))

	avgs_wall = col.OrderedDict()
	avgs_cpu = col.OrderedDict()
	key = ()
	for k,v in dd.items():
		for line in v:
			#print(v[0][0][3])
			for l in line:
				if len(l) >= 3:
					# key is number of threads, process name
					key = tuple([k[0], l[3]])
				if 'wall time' in l[1]:
					if key in avgs_wall.keys():
						avgs_wall[key].append(float(str.split(l[1])[2]))
					else:
						avgs_wall[key] = [ float(str.split(l[1])[2]) ]
				if 'cpu time' in l[0]:
					if key in avgs_cpu.keys():
						avgs_cpu[key].append(float(str.split(l[1])[2]))
					else:
						avgs_cpu[key] = [ float(str.split(l[1])[2]) ]
	
	avgs_cpu1 = col.OrderedDict({ k: round(st.mean(v), 3) for k, v in avgs_cpu.items() })
	avgs_wall1 = col.OrderedDict({ k: round(st.mean(v), 3) for k, v in avgs_wall.items() })

#	for k,v in avgs_cpu1.items():
#		print(k)
#		print(v)

	return avgs_cpu1, avgs_wall1
#	for k,v in avgs_wall1.items():
#		print(k)
#		print(v)

#(('1/48', '1', '48', 'WELL_DO_XCH-BOUNDED;', '1', '1'), ('cpu time 0.6633', 'wall time 0.3321'))

# Do not use Xwindows;
#+  needs to be called before pyplot is imported
import matplotlib 
matplotlib.use('Agg')
import matplotlib.pyplot as plt 


#Need two arrays: cpu_times, threads 
def make_plots(cpu_time, chart_suffix, y_label_name):
	#make a list of all 'chart_name' charts
	charts = col.OrderedDict()
	charts['XCH'] = []
	charts['MTX'] = []
	charts['SPL'] = []
	for k,v in cpu_time.items():
		found = k[1].find(chart_suffix)
		if found > 0:
			if 'WELL_DO_XCH' in k[1]:
				charts['XCH'].append(tuple([k[0],v]))
			if 'WELL_DO_MTX' in k[1]:
				charts['MTX'].append(tuple([k[0],v]))
			if 'WELL_DO_SPL' in k[1]:
				charts['SPL'].append(tuple([k[0],v]))
		
	#for each chart (mtx, xch and spl) and I need a line on the plot
	lin_y_mtx = col.OrderedDict()
	lin_y_xch = col.OrderedDict()
	lin_y_spl = col.OrderedDict()
	for k,v in charts.items():
		for l in v:
			# make a y axis for each chart
			if k == 'MTX':
				lin_y_mtx[l[0]] = l[1]
			if k == 'XCH':
				lin_y_xch[l[0]] = l[1]
			if k == 'SPL':
				lin_y_spl[l[0]] = l[1]

	commit_id = current_commit()
	lin_x = [ i for i in range(min(lin_y_mtx.keys())-1, max(lin_y_mtx.keys())+2) ]
	_, ax = plt.subplots()
	
	ax.set_xlabel('threads')
	ax.set_xticks(lin_x, minor=False)
	ax.set_xticklabels(lin_x)
	all_values = []
	all_values.extend(lin_y_mtx.values())
	all_values.extend(lin_y_xch.values())
	all_values.extend(lin_y_spl.values())
	all_values.sort()
	y_min = min(all_values)
	y_max = max(all_values) 
	step = (y_max - y_min) / 10
	ticks = [ round(y_min + step * i,3) for i in range(10) ]
	ax.set_ylabel(y_label_name)
	ax.set_yticks(ticks, minor=False)
	ax.set_yticklabels(ticks)

	# make sure the arrays are sorted by the thread count (keys)
	lin_y_mtx_sorted = col.OrderedDict(sorted(lin_y_mtx.items(), key=lambda item: item))
	lin_y_xch_sorted = col.OrderedDict(sorted(lin_y_xch.items(), key=lambda item: item))
	lin_y_spl_sorted = col.OrderedDict(sorted(lin_y_spl.items(), key=lambda item: item))
	print(lin_y_spl_sorted)
	x1 = [ i for i in lin_y_mtx_sorted.keys() ]
	y1 = [ i for i in lin_y_mtx_sorted.values() ]
	x2 = [ i for i in lin_y_xch_sorted.keys() ]
	y2 = [ i for i in lin_y_xch_sorted.values() ]
	x3 = [ i for i in lin_y_spl_sorted.keys() ]
	y3 = [ i for i in lin_y_spl_sorted.values() ]

	plt.plot(x1,y1, 'b-', x2, y2, 'g-', x3, y3, 'r-')
	plt.legend(['blue = MTX', 'green = XCH', 'red = SPL'], loc='upper right')
	plt.axis(xmin = min(lin_x), xmax = max(lin_x), ymin = y_min, ymax = y_max)
	plt.savefig('{0} - {1} {2}.pdf'.format(commit_id, y_label_name, chart_suffix), dpi=600, papertype='a4', orientation='landscape')

def main():
	filename = 'meson-logs/benchmarklog.txt'
	runs = col.OrderedDict()

#	option_threads = read_threads_option()
#	print(option_threads)
	for i in range(0, 3):
		print(i)
		run_benchmark()
		runs[i] = parse_file(filename)
	
	avgs_cpu, avgs_wall = summate_runs(runs)

	make_plots(avgs_wall, '-BOUNDED;', 'wall time')
	make_plots(avgs_wall, '-YIELD;', 'wall time')
	make_plots(avgs_wall, '-COUNT;', 'wall time')
	make_plots(avgs_wall, '-SPIN;', 'wall time')

	make_plots(avgs_cpu, '-BOUNDED;', 'cpu time')
	make_plots(avgs_cpu, '-YIELD;', 'cpu time')
	make_plots(avgs_cpu, '-COUNT;', 'cpu time')
	make_plots(avgs_cpu, '-SPIN;', 'cpu time')
if __name__ == "__main__":
	main()
