#	utils
# Any utilities directly provided by this library
fnvsum = executable('fnvsum', 'fnvsum.c',
		include_directories : inc,
		link_with : nonlibc,
		install : do_install)

test('fnvsum test',
	find_program('test_fnvsum.py'),
	args : fnvsum.full_path())

ncp = executable('ncp', 'ncp.c',
		include_directories : inc,
		link_with : nonlibc,
		install : do_install)

test('ncp test',
	find_program('test_ncp.py'),
	args : ncp.full_path())
