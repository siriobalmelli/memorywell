{ system ? builtins.currentSystem, buildtype ? "debug", compiler ? "gcc", linktype ? "true" }:

with import <nixpkgs> { inherit system; };

stdenv.mkDerivation rec {
	name = "memorywell";
	env = buildEnv { name = name; paths = nativeBuildInputs; };
	outputs = [ "out" ];
	nativeBuildInputs = [
		cscope
		pandoc
		(lowPrio gcc)
		clang
		clang-tools
		ninja
		meson
		which
		valgrind
		python3
		pkgconfig
	];
	src = ./.;
	# Override the setupHook in the meson nix derviation,
	# so that meson doesn't automatically get invoked from there.
	meson = pkgs.meson.overrideAttrs ( oldAttrs: rec {
		setupHook = "";
	});
	
	nonlibc1 = callPackage ../nonlibc {};
	postPatch = ''
		PKG_CONFIG_PATH="${nonlibc1.outPath}"/lib/pkgconfig/
	'';

	configurePhase = ''
		echo $PKG_CONFIG_PATH
        	mesonFlags="--prefix=$out $mesonFlags"
    		mesonFlags="--buildtype=${buildtype} $mesonFlags"
		echo $mesonFlags
		sed -i 's/true/'${linktype}'/' meson_options.txt
    		CC=${compiler} meson build $mesonFlags
		cd build
		''; 

	buildPhase = '' 
		ninja test
		ninja install
		'';
}
