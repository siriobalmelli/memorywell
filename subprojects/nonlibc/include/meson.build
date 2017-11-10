##
#	project configuration
##
conf_data = configuration_data()

# TODO: not necessarily true on all systems
conf_data.set('NLC_CACHE_LINE', 64)

conf = configure_file(input : 'nonlibc.h.in',
	      output: 'nonlibc.h',
	      configuration : conf_data)

##
#	headers
##
headers = [ conf, 'atop.h', 'b2hx.h', 'fnv.h', 'hx2b.h', 'nmath.h', 'nmem.h',
		'nlc_urand.h', 'npath.h', 'pcg_rand.h', 'lifo.h', 'zed_dbg.h' ]

# We assume that we will be statically linked if we're a subproject;
#+	ergo: don't pollute the system with our headers
if not meson.is_subproject()
	install_headers(headers)
endif
