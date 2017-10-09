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
  if not meson.is_subproject()
	a_test = executable(t.split('.')[0] + '_shared', t,
			include_directories : inc,
			link_with : well_shared,
			dependencies : [ deps, thread_dep ])
	test(' '.join(t.split('.')[0].split('_')) + ' (shared)', a_test)

	test_static = executable(t.split('.')[0] + '_static', t,
			include_directories : inc,
			link_with : well_static,
			dependencies : [ deps, thread_dep ])
	test(' '.join(t.split('.')[0].split('_')) + ' (static)', test_static)


  else
    a_test = executable(t.split('.')[0], t,
			include_directories : inc,
			link_with : well,
			dependencies : [ deps, thread_dep ])
    test(' '.join(t.split('.')[0].split('_')), a_test, timeout: 60)
  endif
endforeach



##
#	tests: specific
#
# Targeted test cases
##




##
#	benchmarks
##
techniques = [ 'WELL_DO_XCH', 'WELL_DO_MTX', 'WELL_DO_SPL' ]

foreach t : techniques
  test_defines = [ 'YIELD', 'COUNT', 'SPIN' ]
  #test_defines = [ 'SLEEP', 'YIELD', 'COUNT', 'SPIN' ]
  foreach def : test_defines
    a_test = executable(t + '-' + def, [ 'well_test.c', '../src/well.c' ],
			include_directories : inc,
			dependencies : [ deps, thread_dep ],
			c_args : [ '-DFAIL_METHOD=' + def, '-DWELL_TECHNIQUE=' + t])

	thread_counts = [ '1', '2', '3' ]
	foreach tx : thread_counts
	  foreach rx : thread_counts
		benchmark(t + '-' + def + '; ' + tx + '->' + rx,
		  a_test,
		  args : [ '-c', '1024', '-n', '90000000', '-r', '100', '-t', tx, '-x', rx],
		  timeout: 90)
	  endforeach
	endforeach
  endforeach
endforeach
