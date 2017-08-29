# We assume that we will be statically linked if we're a subproject;
#+	ergo: don't pollute the system with our headers
if not meson.is_subproject()
	install_headers( 'cbuf.h', 'cbuf_int.h', 'zcio.h' )
endif
