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

nbuf_test = executable('nbuf_test', 'nbuf_test.c',
			include_directories : inc,
			link_with : cbuf,
			dependencies : [ deps, thread_dep ])
test('nbuf test',
	nbuf_test)

