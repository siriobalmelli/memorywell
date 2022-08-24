{
  nixpkgs ? import (builtins.fetchGit {
    url = "https://siriobalmelli@github.com/siriobalmelli-foss/nixpkgs.git";
    ref = "refs/tags/sirio-2022-08-24";
    }) { }
  }:

with nixpkgs;

let jekyll_env = bundlerEnv rec {
    name = "jekyll_env";
    gemfile = ./Gemfile;
    lockfile = ./Gemfile.lock;
    gemset = ./gemset.nix;
  };
in
  stdenv.mkDerivation rec {
    name = "jekyll_env";
    buildInputs = [ jekyll_env ];

    src = if lib.inNixShell then null else nix-gitignore.gitignoreSource [] ./.;

    shellHook = ''
      exec ${jekyll_env}/bin/jekyll serve --watch
    '';
}
