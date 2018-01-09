lib_files =  [ 'well.c']

# Always build (and test!) both static and shared libraries, but only INSTALL
#+	what the caller intends.
# We rely on the '--default-library' flag of Meson, which defaults to 'shared'
_lib = get_option('lib_type')
if _lib == 'shared'
	lib_shared = true
elif _lib == 'static'
	lib_shared = false
else
	error('-Dlib_type expects "shared" or "static"; got ' + _lib)
endif


well_shared = shared_library(meson.project_name(),
			lib_files,
			include_directories : inc,
			install : lib_shared,
			dependencies : deps)
well_static = static_library(meson.project_name(),
			lib_files,
			include_directories : inc,
			install : not lib_shared,
			dependencies : deps)

if not lib_shared
	well = well_static
else
	well = well_shared
	# Make linking work on linux systems without breaking nix
	p = get_option('prefix')
	if host_machine.system() == 'linux' and not p.startswith('/nix')
		meson.add_install_script('ldconfig', p + '/' + get_option('libdir'))
	endif
endif

# don't set anything here, rely on variables declared in toplevel file
pkg = import('pkgconfig')
pkg.generate(libraries : well,
		name : meson.project_name(),
		version : meson.project_version(),
		filebase : meson.project_name(),
		description : description)
well_dep = declare_dependency(link_with : well, include_directories : inc)
