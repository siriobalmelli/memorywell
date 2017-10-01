thread_dep = dependency('threads')

cbuf_test = executable('cbuf_test', 'cbuf_test.c',
			include_directories : inc,
			link_with : cbuf,
			dependencies : [ deps, thread_dep ])
test('cbuf test',
	cbuf_test)

#cbuf_splice_test = executable('cbuf_splice_test', 'cbuf_splice_test.c',
#			include_directories : inc,
#			link_with : cbuf,
#			dependencies : [ deps, thread_dep ])
#test('cbuf splice test',
#	cbuf_splice_test)

nbuf_test_cas = executable('nbuf_test_cas', 'nbuf_test.c',
			include_directories : inc,
			link_with : nbuf_cas,
			dependencies : [ deps, thread_dep ],
			c_args : ['-DNBUF_TECHNIQUE=NBUF_DO_CAS'])

nbuf_test_xch = executable('nbuf_test_xch', 'nbuf_test.c',
			include_directories : inc,
			link_with : nbuf_xch,
			dependencies : [ deps, thread_dep ],
			c_args : ['-DNBUF_TECHNIQUE=NBUF_DO_XCH'])

nbuf_test_mtx = executable('nbuf_test_mtx', 'nbuf_test.c',
			include_directories : inc,
			link_with : nbuf_mtx,
			dependencies : [ deps, thread_dep ],
			c_args : ['-DNBUF_TECHNIQUE=NBUF_DO_MTX'])

test('nbuf cas fixed',
	nbuf_test_cas,
	args : [ '-r', '32' ],
	is_parallel : false)
test('nbuf cas variable',
	nbuf_test_cas,
	args : [ '-r', '-1' ],
	is_parallel : false)

test('nbuf xch fixed',
	nbuf_test_xch,
	args : [ '-r', '32' ],
	is_parallel : false)
test('nbuf xch variable',
	nbuf_test_xch,
	args : [ '-r', '-1' ],
	is_parallel : false)

test('nbuf mtx fixed',
	nbuf_test_mtx,
	args : [ '-r', '32' ],
	is_parallel : false)
test('nbuf mtx variable',
	nbuf_test_mtx,
	args : [ '-r', '-1' ],
	is_parallel : false)
