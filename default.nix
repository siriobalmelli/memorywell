{ 	# deps
	system ? builtins.currentSystem,
	nixpkgs ? import <nixpkgs> { inherit system; },
	nonlibc ? nixpkgs.nonlibc or import <nonlibc> { inherit system;
							inherit buildtype;
							inherit compiler;
							inherit lib_type;
							inherit dep_type;
							inherit mesonFlags;
	},
	# options
	buildtype ? "release",
	compiler ? "clang",
	lib_type ? "shared",
	dep_type ? "shared",
	mesonFlags ? ""
}:

# note that "nonlibc" above should not be clobbered by this
with nixpkgs;

stdenv.mkDerivation rec {
	name = "memorywell";
	#env = buildEnv { name = name; paths = nativeBuildInputs; };
	outputs = [ "out" ];

	# build-only deps
	nativeBuildInputs = [
		(lowPrio gcc)
		clang-tools
		clang
		cscope
		meson
		ninja
		pandoc
		pkgconfig
		python3
		valgrind
		which
	];

	# runtime deps
	buildInputs = [
		nonlibc
	];

	# just work with the current directory (aka: Git repo), no fancy tarness
	src = ./.;

	# Override the setupHook in the meson nix derviation,
	# so that meson doesn't automatically get invoked from there.
	meson = pkgs.meson.overrideAttrs ( oldAttrs: rec {
		setupHook = "";
	});

	# don't harden away position-dependent speedups for static builds
	hardeningDisable = if lib_type == "static" then
		[ "pic" "pie" ]
	else
		[];

	# build
	mFlags = mesonFlags
		+ " --buildtype=${buildtype}"
		+ " -Dlib_type=${lib_type}"
		+ " -Ddep_type=${dep_type}";

	configurePhase = ''
		echo "flags: $mFlags"
		echo "prefix: $out"
		CC=${compiler} meson --prefix=$out build $mFlags
		cd build
		''; 

	buildPhase = "ninja";
	doCheck = true;
	checkPhase = "ninja test";
	installPhase = "ninja install";
}
