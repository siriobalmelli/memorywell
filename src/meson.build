lib_files =  [ 'nbuf.c']
#lib_files = [ 'cbuf.c', 'cbuf_checkpoint.c', 'cbuf_int.c' ]
# zcio.c


nbuf_shared = shared_library(meson.project_name(),
			lib_files,
			include_directories : inc,
			install : true,
			dependencies : deps)
nbuf_static = static_library(meson.project_name(),
			lib_files,
			include_directories : inc,
			install : false,
			dependencies : deps)

# TODO: fix namiiiiing
if meson.is_subproject()
	cbuf = nbuf_static
	cbuf_dep = declare_dependency(link_with : nonlibc,
					include_directories : inc)
else
	cbuf = nbuf_shared
	# don't set anything here, rely on variables declared in toplevel file
	pkg = import('pkgconfig')
	pkg.generate(libraries : cbuf,
			name : meson.project_name(),
			version : meson.project_version(),
			filebase : meson.project_name(),
			description : description)
endif
