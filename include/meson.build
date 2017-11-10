##
#	project configuration
##
conf_data = configuration_data()

#	contention strategies
conf_data.set('WELL_DO_CAS',		'1') # not currently implemented
conf_data.set('WELL_DO_XCH',		'2') # lock-free exchange
conf_data.set('WELL_DO_MTX',		'3') # take a mutex
conf_data.set('WELL_DO_SPL',		'4') # mutex replaced with naive spinlock
# preferred technique is lock-free exchange
conf_data.set('WELL_TECHNIQUE', conf_data.get('WELL_DO_XCH'))

#	failure strategies
conf_data.set('WELL_FAIL_SPIN',		'1')
conf_data.set('WELL_FAIL_YIELD',	'3')
conf_data.set('WELL_FAIL_SLEEP',	'4')
conf_data.set('WELL_FAIL_SIGNAL',	'5')
conf_data.set('WELL_FAIL_BOUNDED',	'6')
# preferred failure method is bounded sleep
conf_data.set('WELL_FAIL_METHOD', conf_data.get('WELL_FAIL_BOUNDED'))

conf = configure_file(input : 'well_config.h.in',
	      output: 'well_config.h',
	      configuration : conf_data)

##
#	headers
##
headers = [ 'well.h', 'well_fail.h', conf ]

# We assume that we will be statically linked if we're a subproject;
#+  ergo: don't pollute the system with our headers
if not meson.is_subproject()
  install_headers(headers)
endif

