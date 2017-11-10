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

def run_single(test_exc, block_size, reservation):
    '''run nbuf_test_cas once with 'block_size' and 'reservation'

    returns:
           'numiter 100000000; blk_size 16; blk_count 8; reservation 4
            TX/RX reservation 4
            cpu time: 30.9824s
    ''' 
    try:
	    #TODO: This is hardcoded. That is bad.
	    sub = subprocess.run([test_exc, "-s {}".format(block_size), "-r {}".format(reservation)],
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


import numpy as np 

#block_size / reservation and cpu_time

def gen_benchmark(executable):
    '''actually run the benchmark.
    Return a dictionary of results.
    '''
    ret = {}

    ret['commit-id'] = current_commit()

    # block_size comes in powers of two starting at 8, until what size??? 
    ret['X_block_size'] = [ 2**j for j in range(3, 8) ]
    ret['Y_reservation'] = [ 2**sz_exp 
                for sz_exp in range(1, 8) ] 

    # Generate mesh of graphing coordinates against which
    #+  to run tests (so they execute in the right order!)
    X, Y = np.meshgrid(ret['X_block_size'], ret['Y_reservation'])

    # execute the runs
    # NOTE: don't use a list comprehension: avoid map(list, zip())
    #+  and more explicitly show loop nesting (important when making a mesh to plot, later)

	# NOTE: This generates our Z axis. Here we run our tests to generate our
	#+ cpu_times.
    ret['Z_cpu_time'] = list(run_single(test_exc = executable, block_size = X[i][j], reservation = Y[i][j])
							for i in range(len(X))
							for j in range(len(X[0]))
						)

    return ret

from math import log
# Do not use Xwindows;
#+  needs to be called before pyplot is imported
import matplotlib 
matplotlib.use('Agg')
import matplotlib.pyplot as plt 
from matplotlib import cm

def make_plots(bm, exe):
    lin_x = bm['X_block_size'] # for legibility only
    log_y = [ log(y, 2) for y in bm['Y_reservation'] ]
    tick_y = [ y for y in bm['Y_reservation'] ] # used for labels
    X, Y = np.meshgrid(lin_x, log_y)

    # Repeat plotting work for the following:
    do_plots = { "Z_cpu_time" : ("CPU Time", "CPU Time"),
                }

    for k, v in do_plots.items():
        lin_z = bm[k]
        Z = np.array(lin_z).reshape(X.shape) 

        _, ax = plt.subplots()
        p = plt.pcolormesh(X, Y, Z, cmap=cm.get_cmap('Vega20'), axes=ax,
                            vmin=bm[k][0], vmax=bm[k][-1])

        ax.set_xlabel('block size')
        ax.set_xticks(lin_x, minor=False)
        ax.set_ylabel('reservation')
        ax.set_yticks(log_y, minor=False)
        ax.set_yticklabels(tick_y)

        # standard label formatting
        ax.tick_params(labelsize = 5)
        ax.set_frame_on(False)
        ax.grid(True)

        # colorbar
        cb = plt.colorbar(p, ax=ax, orientation='horizontal', aspect=20)
        cb.ax.set_xlabel(v[0])
        # generate precisely 20 tick values, so they will fit on the borders
        #+  of the 20 colormap squares
        step = (lin_z[-1] - lin_z[0]) / 20
        ticks = [ lin_z[0] + step * i for i in range(20) ]
        cb.set_ticks(ticks)
        # truncate the tick label to 5 characters without affecting the values
        cb.set_ticklabels([ '%.5s' % z for z in ticks ])
        # standard label formatting
        cb.ax.tick_params(labelsize = 5)
        cb.ax.set_frame_on(False)
        cb.ax.grid(True)

        # layout; save
        plt.title('nbuf lib benchmark; commit {0}'.format(bm['commit-id']))
        plt.tight_layout()
        plt.savefig('{0} - {1} - {2}.pdf'.format(bm['commit-id'], v[1], exe), dpi=600,
                    papertype='a4', orientation='landscape')

import json
import os

def main():
    for exe in executables:
        filename = 'nbuf_bench_{0}.json'.format(os.path.basename(exe))
        bm = gen_benchmark(exe)
        with open(filename, 'w') as f:
             json.dump(bm, f)

        make_plots(bm, os.path.basename(exe))


if __name__ == '__main__':
	main()
