{
  description = "CS144 Minnow development environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            cmake
            gnumake
            ninja

            libllvm
            clang-tools

            gdb
          ];

          shellHook = ''
            echo "CS144 Minnow dev environment ready (C++23, CMake)"
          '';
        };
      });
}
