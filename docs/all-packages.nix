{ system ? builtins.currentSystem, 
  lib_type ? "shared",
  dep_type ? "shared"
}:

let
  pkgs = import <nixpkgs> { inherit system; };

  callPackage = pkgs.lib.callPackageWith (pkgs // self);

  self = {
	nonlibc = callPackage ./nonlibc { };

	memorywell = callPackage ./memorywell { inherit lib_type; inherit dep_type; };
  };
in
self
