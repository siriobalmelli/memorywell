{ 	system ? builtins.currentSystem,
	buildtype ? "release",
	compiler ? "gcc",
	lib_type ? "shared",
	dep_type ? "shared",
	mesonFlags ? ""
}:

with import <nixpkgs> { inherit system; };

stdenv.mkDerivation rec {
	name = "memorywell";
	env = buildEnv { name = name; paths = nativeBuildInputs; };
	outputs = [ "out" ];
	nativeBuildInputs = [
		(lowPrio gcc)
		clang
		clang-tools
		cscope
		meson
		ninja
		pandoc
		pkgconfig
		python3
		valgrind
		which
	];

	# import nonlibc TODO: fixthis
	nonlibc1 = callPackage ../nonlibc { inherit buildtype; inherit compiler; 
					lib_type = dep_type; inherit dep_type; };
	postPatch = ''
		PKG_CONFIG_PATH="${nonlibc1.outPath}"/lib/pkgconfig/
	'';

	# just work with the current directory (aka: Git repo), no fancy tarness
	src = ./.;

	# Override the setupHook in the meson nix derviation,
	# so that meson doesn't automatically get invoked from there.
	meson = pkgs.meson.overrideAttrs ( oldAttrs: rec {
		setupHook = "";
	});

	# build
	mFlags = mesonFlags
		+ " --buildtype=${buildtype}"
		+ " -Dlib_type=${lib_type}"
		+ " -Ddep_type=${dep_type}";
	configurePhase = ''
		echo "pkgconfig: $PKG_CONFIG_PATH"
		echo "flags: $mFlags"
		echo "prefix: $out"
		CC=${compiler} meson --prefix=$out build $mFlags
		cd build
		''; 

	buildPhase = '' 
		ninja test
		ninja install
		'';
}
