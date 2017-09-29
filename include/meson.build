# We assume that we will be statically linked if we're a subproject;
#+  ergo: don't pollute the system with our headers
if not meson.is_subproject()
  install_headers( 'cbuf.h', 'cbuf_int.h', 'nbuf.h' )
#zcio.h
endif

conf_data = configuration_data()
conf_data.set('version', '0.1.0')

conf_data.set('NBUF_DO_CAS', '1')
conf_data.set('NBUF_DO_XCH', '2')
conf_data.set('NBUF_DO_MTX', '3')

configure_file(input : 'config.h.in',
	      output: 'config.h',
	      configuration : conf_data)
