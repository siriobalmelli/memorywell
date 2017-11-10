tests = [
'atop_test.c',
'fnv_test.c',
'hex2bin2hex_test.c',
'lifo_test.c',
'nlc_urand_test.c',
'nmath_test.c',
'npath_test.c',
'pcg_rand_test.c',
'zed_dbg_test.c' ]

foreach t : tests
  name = t.split('.')[0]
  name_spaced = ' '.join(name.split('_'))

	# if building as own project, test both shared and statically-linked tests
	if not meson.is_subproject()
		a_test = executable(name + '_shared', t,
				include_directories : inc,
				link_with : nonlibc_shared)
		test(name_spaced + ' (shared)', a_test)

		test_static = executable(name + '_static', t,
				include_directories : inc,
				link_with : nonlibc_static)
		test(name_spaced + ' (static)', test_static)

	# otherwise, just test whatever we are exporting to the superproject
	else
		a_test = executable(name, t,
				include_directories : inc,
				link_with : nonlibc)
		test(name_spaced, a_test)
	endif
endforeach
