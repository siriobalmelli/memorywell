##
#	project configuration
##
conf_data = configuration_data()

conf_data.set('WELL_DO_CAS', '1') # not currently implemented
conf_data.set('WELL_DO_XCH', '2') # lock-free exchange
conf_data.set('WELL_DO_MTX', '3') # take a mutex
conf_data.set('WELL_DO_SPL', '4') # mutex replaced with naive spinlock
# preferred technique is lock-free exchange
conf_data.set('WELL_TECHNIQUE', conf_data.get('WELL_DO_XCH'))

conf = configure_file(input : 'well_config.h.in',
	      output: 'well_config.h',
	      configuration : conf_data)

##
#	headers
##
headers = [ 'well.h', conf ]

# We assume that we will be statically linked if we're a subproject;
#+  ergo: don't pollute the system with our headers
if not meson.is_subproject()
  install_headers(headers)
endif

