#!/usr/bin/env python3

import subprocess

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


import re
rex = re.compile('.*cpu time: ([0-9]+).([0-9]+)s', re.DOTALL)

executables = ['test/NBUF_DO_SPL',
               'test/NBUF_DO_XCH',
               'test/NBUF_DO_MTX']

def run_single(test_exc, reservation):
    '''run nbuf_test_cas once with 'block_size' and 'reservation'

    returns:
           'numiter 100000000; blk_size 16; blk_count 8; reservation 4
            TX/RX reservation 4
            cpu time: 30.9824s
    ''' 
    try:
	    #TODO: This is hardcoded. That is bad.
	    sub = subprocess.run([test_exc, "-r {}".format(reservation)],
		                     stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             shell=False, check=True);
    except subprocess.CalledProcessError as err:
               print(err.cmd)
               print(err.stdout.decode('ascii'))
               print(err.stderr.decode('ascii'))
               exit(1)

    res = rex.match(sub.stdout.decode('ascii'))
    try:
        return float("{0}.{1}".format(res.group(1), res.group(2)))
    except:
        print("regex didn't match!")
        print(sub.stdout.decode('ascii'))
        exit (1)


#import numpy as np 

#block_size / reservation and cpu_time

from collections import OrderedDict

ret = OrderedDict()
def gen_benchmark(executable):
    '''actually run the benchmark.
    Return a dictionary of results.
    '''

    ret['commit-id'] = current_commit()

    # block_size comes in powers of two starting at 8, until what size??? 
    ret['X_reservation'] = [ 2**sz_exp 
                for sz_exp in range(1, 8) ] 

	# NOTE: This generates our Z axis. Here we run our tests to generate our
	#+ cpu_times.
    line_name = '{0}'.format(os.path.basename(executable))
    ret[line_name] = list(run_single(test_exc = executable, reservation = ret['X_reservation'][i])
							for i in range(len(ret['X_reservation']))
						)
    print(ret)
    return ret

# Do not use Xwindows;
#+  needs to be called before pyplot is imported
import matplotlib 
matplotlib.use('Agg')
import matplotlib.pyplot as plt 
#from matplotlib import cm
from math import log
def make_plots(bm):
	commit_id = ''
	lin_y = []
	lin_x = []
	for k,v in bm.items():
		if k == 'NBUF_DO_SPL' or k == 'NBUF_DO_XCH' or k == 'NBUF_DO_MTX':
			lin_y.extend(v)
		else:
			lin_x = v
	lin_y.sort()
	for k,v in bm.items():
		if k == 'commit-id':
			commit_id = v
		_, ax = plt.subplots()
		
		tickx = [ int(log(i,2)) for i in lin_x ]
		ax.set_xlabel('reservation')
		ax.set_xticks(tickx, minor=False)
		ax.set_xticklabels(tickx)
		step = (max(lin_y) - min(lin_y)) / 20
		ticks = [ min(lin_y) + step * i for i in range(20) ]
		ax.set_ylabel('cpu time')
		ax.set_yticks(ticks, minor=False)
		ax.set_yticklabels(lin_y)

	x1 = bm['X_reservation']
	y1 = bm['NBUF_DO_MTX']

	x2 = bm['X_reservation']
	y2 = bm['NBUF_DO_XCH']

	x3 = bm['X_reservation']
	y3 = bm['NBUF_DO_SPL']
	plt.plot(x1,y1, 'b-', x2, y2, 'g-', x3, y3, 'r-')
	plt.legend(['blue = MTX', 'green = XCH', 'red = SPL'], loc='upper right')
	plt.axis(xmin = 1, xmax = 8, ymin = min(lin_y), ymax = max(lin_y))
	plt.savefig('{0} - {1}.pdf'.format(commit_id, 'CPU Time'), dpi=600, papertype='a4', orientation='landscape')


import json
import os

def main():
    bm = {}
    for exe in executables:
        print('running exe {0}'.format(os.path.basename(exe)))
        filename = 'nbuf_bench_{0}.json'.format(os.path.basename(exe))
   #     line_name = 'Y_cpu_time_{0}'.format(os.path.basename(exe))
        bm = gen_benchmark(exe)
        with open(filename, 'w') as f:
             json.dump(bm, f)

    make_plots(bm)


if __name__ == '__main__':
	main()
