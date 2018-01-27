{ 	system ? builtins.currentSystem,
	buildtype ? "release",
	compiler ? "gcc",
	lib_type ? "shared",
	dep_type ? "shared",
	mesonFlags ? "",
	nonlibc
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
		ninja
		nonlibc
		meson
		ninja
		pandoc
		pkgconfig
		python3
		valgrind
		which
	];

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
