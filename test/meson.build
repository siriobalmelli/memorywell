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

	a_test = executable(name + '_shared', t,
		    include_directories : inc,
		    link_with : well_shared,
		    dependencies : [ deps, thread_dep ])
	test(name_spaced + ' (shared)', a_test)

	test_static = executable(name + '_static', t,
		    include_directories : inc,
		    link_with : well_static,
		    dependencies : [ deps, thread_dep ])
	test(name_spaced + ' (static)', test_static)
endforeach



##
#	test all threading combinations for all contention techniques
##
techniques = [ 'WELL_DO_CAS', 'WELL_DO_XCH', 'WELL_DO_MTX', 'WELL_DO_SPL' ]
base_args = [ '-c', '1024', '-n', '9000000', '-r', '100' ]

foreach t : techniques
  a_test = executable(t, [ 'well_test.c', '../src/well.c' ],
		      include_directories : inc,
		      dependencies : [ deps, thread_dep ],
		      c_args : [ '-DWELL_TECHNIQUE=' + t])

  test(t + ' ' + '1->1', a_test, args : base_args + ['-t', '1', '-x', '1'], is_parallel : false)
  test(t + ' ' + '1->2', a_test, args : base_args + ['-t', '1', '-x', '2'], is_parallel : false)
  test(t + ' ' + '2->1', a_test, args : base_args + ['-t', '2', '-x', '1'], is_parallel : false)
  test(t + ' ' + '2->2', a_test, args : base_args + ['-t', '2', '-x', '2'], is_parallel : false)
endforeach
