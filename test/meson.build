##
#	tests: vanilla
#
# Run every test executable with both shared and static linkage
##
thread_dep = dependency('threads')

tests = [
  'nbuf_test.c'
]

# TODO: naming
foreach t : tests
  if not meson.is_subproject()
	a_test = executable(t.split('.')[0] + '_shared', t,
			include_directories : inc,
			link_with : nbuf_shared,
			dependencies : [ deps, thread_dep ])
	test(' '.join(t.split('.')[0].split('_')) + ' (shared)', a_test)

	test_static = executable(t.split('.')[0] + '_static', t,
			include_directories : inc,
			link_with : nbuf_static,
			dependencies : [ deps, thread_dep ])
	test(' '.join(t.split('.')[0].split('_')) + ' (static)', test_static)


  else
    a_test = executable(t.split('.')[0], t,
			include_directories : inc,
			link_with : cbuf,
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
techniques = [ 'NBUF_DO_XCH', 'NBUF_DO_MTX', 'NBUF_DO_SPL' ]

foreach t : techniques
  test_defines = [ 'YIELD', 'COUNT', 'SPIN' ]
  #test_defines = [ 'SLEEP', 'YIELD', 'COUNT', 'SPIN' ]
  foreach def : test_defines
    a_test = executable(t + '-' + def, [ 'nbuf_test.c', '../src/nbuf.c' ],
			include_directories : inc,
			dependencies : [ deps, thread_dep ],
			c_args : [ '-DFAIL_METHOD=' + def, '-DNBUF_TECHNIQUE=' + t])

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
