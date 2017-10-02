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
#rex = re.compile('.*blk_size ([0-9]+); blk_count ([0-9]+); reservation ([0-9]+)', re.DOTALL)
rex = re.compile('.*cpu time: ([0-9]+).([0-9]+)s', re.DOTALL)

executables = ['build-debug/test/nbuf_test_cas', 
               'build-debug/test/nbuf_test_xch', 
               'build-debug/test/nbuf_test_mtx']

def run_single(test_exc, block_count, block_size, reservation):
    '''run nbuf_test_cas once with 'block_size','block_count' and 'reservation'

    returns:
           'numiter 100000000; blk_size 16; blk_count 8; reservation 4
            TX/RX reservation 4
            cpu time: 30.9824s
    ''' 
    try:
	    #TODO: This is hardcoded. That is bad.
	    sub = subprocess.run([test_exc, "-c {}".format(block_count), "-s {}".format(block_size), "-r {}".format(reservation)],
		                     stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             shell=False, check=True);
    except subprocess.CalledProcessError as err:
               print(err.cmd)
               print(err.stdout.decode('ascii'))
               print(err.stderr.decode('ascii'))
               exit(1)

    res = rex.match(sub.stdout.decode('ascii'))
    try:
        return "{0}.{1}".format(res.group(1), res.group(2))
    except Exception as e:
        print(e)
        print("regex didn't match!")
        print(sub.stdout.decode('ascii'))
        exit (1)

def run_average():
	pass

def human_format():
	pass


import numpy as np 

def run_multiple(block_count, block_size, reservation):
	# powers of two block count
    for bc in (2**p for p in range(2, block_count)):
        for bs in (2**p for p in range(3, int(log(block_size,2)))):
            print("running with block_count {0}, block_size {1}".format(bc, bs))
            cpu_time = run_single(executables[0], bc, bs, 1)
            print("cpu time: {0}".format(cpu_time))


from math import log
# Do not use Xwindows;
#+  needs to be called before pyplot is imported
import matplotlib 
matplotlib.use('Agg')
import matplotlib.pyplot as plt 
from matplotlib import cm

def make_plots(bm):
    lin_x = bm['X_ratio'] # for legibility only
    log_y = [ log(y, 2) for y in bm['Y_size'] ]
    tick_y = [ human_format(y) for y in bm['Y_size'] ] # used for labels
    X, Y = np.meshgrid(lin_x, log_y)

    # Repeat plotting work for the following:
    do_plots = { "Z_inef" : ("Inefficiency", "inefficiency"),
                "Z_enc" : ("Encode Bitrate (Gb/s)", "encode" ),
                "Z_dec" : ("Decode Bitrate (Gb/s)", "decode" )
                }

    for k, v in do_plots.items():
        lin_z = bm[k]
        Z = np.array(lin_z).reshape(X.shape) 

        _, ax = plt.subplots()
        p = plt.pcolormesh(X, Y, Z, cmap=cm.get_cmap('Vega20'), axes=ax,
                            vmin=bm[k][0], vmax=bm[k][-1])

        ax.set_xlabel('ratio (ex: 1.011 == 1.1% FEC)')
        ax.set_xticks(lin_x, minor=False)
        ax.set_ylabel('block size (log2)')
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
        plt.title('ffec lib benchmark; commit {0}'.format(bm['commit-id']))
        plt.tight_layout()
        plt.savefig('{0} - {1}.pdf'.format(bm['commit-id'], v[1]), dpi=600,
                    papertype='a4', orientation='landscape')

def main():
#	run_single(executables[1], 4, 256, 2)
    run_multiple(8, 256, 1)


if __name__ == '__main__':
	main()


