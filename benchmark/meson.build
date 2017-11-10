##
#	benchmark for each wait strategy
##
techniques = [ 'WELL_DO_CAS', 'WELL_DO_XCH', 'WELL_DO_MTX', 'WELL_DO_SPL' ]
fail_strat = [ 'WELL_FAIL_SPIN', 'WELL_FAIL_YIELD', 'WELL_FAIL_SLEEP', 'WELL_FAIL_BOUNDED' ]

thread_counts = [ '0', '1', '2', '4', '8', '16', '32' ]

foreach t : techniques
  foreach d : fail_strat
    name = '_'.join(['WELL', t.split('_')[-1], d.split('_')[-1]])
    a_bench = executable(name, [ 'well_bench.c', '../src/well.c' ],
			include_directories : inc,
			dependencies : [ deps, thread_dep ],
			c_args : [ '-DWELL_FAIL_METHOD=' + d, '-DWELL_TECHNIQUE=' + t])
    foreach c : thread_counts
      benchmark(name + ' ' + c, a_bench, args : [ '-s', '5', '-t', c])
    endforeach
  endforeach
endforeach
