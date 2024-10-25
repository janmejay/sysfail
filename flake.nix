{
  description = "SysFail dev-shell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      devShells."${system}" = {
        default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            cmake
            gcc
            gnumake
            pkg-config
            gdb
            valgrind
          ];

          buildInputs = with pkgs; [
            boost
            eigen
            gtest
            tbb_2021_11
          ];

          shellHook = ''
            export CC=gcc
            export CXX=g++
          '';
        };
      };
  };
}
