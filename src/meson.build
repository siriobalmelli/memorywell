lib_files =  [ 'well.c']


well_shared = shared_library(meson.project_name(),
			lib_files,
			include_directories : inc,
			install : true,
			dependencies : deps)
well_static = static_library(meson.project_name(),
			lib_files,
			include_directories : inc,
			install : false,
			dependencies : deps)


if meson.is_subproject()
	well = well_static
	well_dep = declare_dependency(link_with : well,
					include_directories : inc)
else
	well = well_shared
	# don't set anything here, rely on variables declared in toplevel file
	pkg = import('pkgconfig')
	pkg.generate(libraries : well,
			name : meson.project_name(),
			version : meson.project_version(),
			filebase : meson.project_name(),
			description : description)
endif
