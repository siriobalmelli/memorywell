# We assume that we will be statically linked if we're a subproject;
#+  ergo: don't pollute the system with our headers
if not meson.is_subproject()
  install_headers( 'nbuf.h' )
#install_headers( 'cbuf.h', 'cbuf_int.h', 'zcio.h' )
endif

conf_data = configuration_data()

conf_data.set('NBUF_DO_CAS', '1') #not currently implemented
conf_data.set('NBUF_DO_XCH', '2')
conf_data.set('NBUF_DO_MTX', '3')
conf_data.set('NBUF_DO_SPL', '4')
conf_data.set('NBUF_TECHNIQUE', conf_data.get('NBUF_DO_XCH'))

configure_file(input : 'config.h.in',
	      output: 'config.h',
	      configuration : conf_data)
