thread_dep = dependency('threads')

tests = [
  'nbuf_test.c',
# cbuf_test.c,
# cbuf_splice_test.c,
]

foreach t : tests
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
endforeach


nbuf_test_cas = executable('nbuf_test_cas',
			[ 'nbuf_test.c', '../src/nbuf.c' ],
			include_directories : inc,
			dependencies : [ deps, thread_dep ],
			c_args : ['-DNBUF_TECHNIQUE=NBUF_DO_CAS'])
test('nbuf cas fixed',
	nbuf_test_cas,
	args : [ '-r', '64' ],
	is_parallel : false)
test('nbuf cas variable',
	nbuf_test_cas,
	args : [ '-r', '64', '-e' ],
	is_parallel : false)


nbuf_test_xch = executable('nbuf_test_xch', 'nbuf_test.c',
			include_directories : inc,
			link_with : nbuf_xch,
			dependencies : [ deps, thread_dep ],
			c_args : ['-DNBUF_TECHNIQUE=NBUF_DO_XCH'])
test('nbuf xch fixed',
	nbuf_test_xch,
	args : [ '-r', '64' ],
	is_parallel : false)
test('nbuf xch variable',
	nbuf_test_xch,
	args : [ '-r', '64', '-e' ],
	is_parallel : false)


nbuf_test_mtx = executable('nbuf_test_mtx', 'nbuf_test.c',
			include_directories : inc,
			link_with : nbuf_mtx,
			dependencies : [ deps, thread_dep ],
			c_args : ['-DNBUF_TECHNIQUE=NBUF_DO_MTX'])
test('nbuf mtx fixed',
	nbuf_test_mtx,
	args : [ '-r', '64' ],
	is_parallel : false,
	timeout: 60)
test('nbuf mtx variable',
	nbuf_test_mtx,
	args : [ '-r', '64', '-e' ],
	is_parallel : false,
	timeout: 60)
