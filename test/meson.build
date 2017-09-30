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
test('nbuf cas test',
	nbuf_test_cas,
	is_parallel : false,
	timeout : 1000)

test('nbuf xch test',
	nbuf_test_xch,
	is_parallel : false,
	timeout : 1000)

test('nbuf mtx test',
	nbuf_test_mtx,
	is_parallel : false,
	timeout : 2000)
