thread_dep = dependency('threads')

tests = [
  'nbuf_test.c',
# cbuf_test.c,
# cbuf_splice_test.c,
]

foreach t : tests
  if not meson.is_subproject()
	a_test = executable(t.split('.')[0] + '_shared', t,
			include_directories : inc,
			link_with : nbuf_shared,
			dependencies : [ deps, thread_dep ])
	test(' '.join(t.split('.')[0].split('_')) + ' shared', a_test,
	    timeout: 60)

	test_static = executable(t.split('.')[0] + '_static', t,
			include_directories : inc,
			link_with : nbuf_static,
			dependencies : [ deps, thread_dep ])
	test(' '.join(t.split('.')[0].split('_')) + ' static', test_static,
	    timeout: 60)


  else
    a_test = executable(t.split('.')[0], t,
			include_directories : inc,
			link_with : cbuf,
			dependencies : [ deps, thread_dep ])
    test(' '.join(t.split('.')[0].split('_')), a_test, timeout: 60)
  endif
endforeach


techniques = [ 'NBUF_DO_XCH', 'NBUF_DO_MTX', 'NBUF_DO_SPL' ]

foreach t : techniques
  test_defines = [ 'YIELD', 'COUNT', 'SPIN' ]
  foreach def : test_defines
    a_test = executable(t + '-' + def, [ 'nbuf_test.c', '../src/nbuf.c' ],
			include_directories : inc,
			dependencies : [ deps, thread_dep ],
			c_args : [ '-DFAIL_METHOD=' + def, '-DNBUF_TECHNIQUE=' + t])

	thread_counts = [ '1', '2' ]
	foreach tx : thread_counts
	  foreach rx : thread_counts
		test(t + '-' + def + '; ' + tx + '->' + rx,
		  a_test,
		  args : [ '-c', '1024', '-r', '64', '-t', tx, '-x', rx],
		  #		  args : [ '-n', '10000', '-c', '1024', '-r', '100', '-t', tx, '-x', rx],
		  is_parallel : false)
	  endforeach
	endforeach
  endforeach
endforeach
