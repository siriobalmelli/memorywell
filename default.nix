{
  # options
  buildtype ? "release",
  compiler ? "clang",
  dep_type ? "shared",
  mesonFlags ? "",

  # deps
  system ? builtins.currentSystem,
  nixpkgs ? import (builtins.fetchGit {
    url = "https://github.com/siriobalmelli-foss/nixpkgs.git";
    ref = "master";
    }) {}
}:

with nixpkgs;

stdenv.mkDerivation rec {
  name = "memorywell";
  version = "0.2.1";
  outputs = [ "out" ];

  meta = with nixpkgs.stdenv.lib; {
    description = "nonblocking circular buffer";
    homepage = https://siriobalmelli.github.io/memorywell/;
    license = licenses.lgpl21Plus;
    platforms = platforms.unix;
    maintainers = [ "https://github.com/siriobalmelli" ];
  };

  nonlibc = nixpkgs.nonlibc or import (builtins.fetchGit {
    url = "https://github.com/siriobalmelli/nonlibc.git";
    ref = "master";
    }) {};

  inputs = [
    nixpkgs.gcc
    nixpkgs.clang
    nixpkgs.meson
    nixpkgs.ninja
    nixpkgs.pandoc
    nixpkgs.pkgconfig
  ];
  buildInputs = if ! lib.inNixShell then inputs else inputs ++ [
    nixpkgs.cscope
    nixpkgs.gdb
    nixpkgs.man
    nixpkgs.valgrind
    nixpkgs.which
  ];
  propagatedBuildInputs = [
    nonlibc
  ];


  # just work with the current directory (aka: Git repo), no fancy tarness
  src = nixpkgs.nix-gitignore.gitignoreSource [] ./.;

  # Override the setupHook in the meson nix derivation,
  # so that meson doesn't automatically get invoked from there.
  meson = nixpkgs.pkgs.meson.overrideAttrs ( oldAttrs: rec { setupHook = ""; });

  # don't harden away position-dependent speedups for static builds
  hardeningDisable = [ "pic" "pie" ];

  # build
  mFlags = mesonFlags
    + " --buildtype=${buildtype}"
    + " -Ddep_type=${dep_type}";

  configurePhase = ''
      echo "flags: $mFlags"
      echo "prefix: $out"
      CC=${compiler} meson --prefix=$out build $mFlags
      cd build
  '';

  buildPhase = ''
      ninja
  '';
  doCheck = true;
  checkPhase = ''
      ninja test
  '';
  installPhase = ''
      ninja install
  '';
}
