##
#	tests: vanilla
#
# Run every test executable with both shared and static linkage
##
thread_dep = dependency('threads')

tests = [
  'well_test.c',
  'well_validate.c'
]

foreach t : tests
  name = t.split('.')[0]
  name_spaced = ' '.join(name.split('_'))

  # if standalone, test both linkages
  if not meson.is_subproject()
    a_test = executable(name + '_shared', t,
		    include_directories : inc,
		    link_with : well_shared,
		    dependencies : [ deps, thread_dep ])
    test(name_spaced + ' (shared)', a_test)

    # always test static linkage
    test_static = executable(name + '_static', t,
		    include_directories : inc,
		    link_with : well_static,
		    dependencies : [ deps, thread_dep ])
    test(name_spaced + ' (static)', test_static)

  # otherwise, test whatever we are exporting
  else
    a_test = executable(name, t,
		    include_directories : inc,
		    link_with : well,
		    dependencies : [ deps, thread_dep ])
    test(name_spaced, a_test)
  endif
endforeach



##
#	some tests are sometimes benchmarks ;)
##
techniques = [ 'WELL_DO_XCH', 'WELL_DO_MTX', 'WELL_DO_SPL' ]

foreach t : techniques
  test_defines = [ 'BOUNDED', 'YIELD', 'COUNT', 'SPIN' ]
  #test_defines = [ 'SLEEP', 'YIELD', 'COUNT', 'SPIN' ]
  foreach def : test_defines
    a_test = executable(t + '-' + def, [ 'well_test.c', '../src/well.c' ],
			include_directories : inc,
			dependencies : [ deps, thread_dep ],
			c_args : [ '-DFAIL_METHOD=' + def, '-DWELL_TECHNIQUE=' + t])

    # test only the multithreaded case for XCH
    if t == 'WELL_DO_XCH' and def == 'YIELD'
      test(t + '-' + def + '; 2->2', a_test,
	args : [ '-c', '1024', '-n', '90000000', '-r', '100', '-t', '2', '-x', '2'],
	is_parallel : false)
    endif

    ##
    # benchmarks!
    # 
    # The rationale here is to try and highlight contention and scheduling
    #	effects by:
    #
    #	  - reducing buffer size so it fits in L1 (hide memory effects)
    #	  - reserving only one block at a time (worst possible contention)
    #	  - modest number of iterations (suppress noise but don't take forever)
    #
    # NOTE there are occational disastrous performance issues running SPIN or COUNT
    #	versions for a *total* thread-count of `system_cpu_count + 2`.
    # For example on a 2-core hyperthreading machine (system_cpu_count = 4),
    #	the highest sane thread-count is 4+2=6; which means 3 TX and 3 RX.
    ##
    thread_counts = [ '1', '2', '3' ]
    foreach tx : thread_counts
      foreach rx : thread_counts
	    benchmark(t + '-' + def + '; ' + tx + '->' + rx, a_test,
	      args : [ '-c', '128', '-n', '3000000', '-t', tx, '-x', rx],
	      timeout: 90)
      endforeach
    endforeach
  endforeach
endforeach
