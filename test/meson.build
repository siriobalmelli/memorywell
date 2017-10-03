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


techniques = [ 'NBUF_DO_CAS', 'NBUF_DO_XCH', 'NBUF_DO_MTX' ]

foreach t : techniques
  a_test = executable(t, [ 'nbuf_test.c', '../src/nbuf.c' ],
		      include_directories : inc,
		      dependencies : [ deps, thread_dep ],
		      c_args : ['-DNBUF_TECHNIQUE=' + t])
  test(t + ' fixed', a_test,
	args : [ '-r', '64' ],
	is_parallel : false)
  test(t + ' variable', a_test,
	args : [ '-r', '64', '-e' ],
	is_parallel : false)
endforeach
