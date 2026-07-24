{
  description = "husk: M2/M3/WMO -> common-format CLI";

  inputs = {
    pins.url = "path:/home/luna/nix/pins";
    nixpkgs.follows = "pins/nixpkgs";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    inputs@{
      self,
      pins,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };
        lib = pkgs.lib;

        projectRoot = ../.;

        # Only what the packaged build actually needs: CMakeLists.txt + src/.
        # Deliberately excludes tests/, build/, .git, .reference/ -- none of
        # those should ever end up copied into the Nix store for this.
        appSource = lib.fileset.toSource {
          root = projectRoot;
          fileset = lib.fileset.unions [
            (projectRoot + "/CMakeLists.txt")
            (projectRoot + "/src")
          ];
        };

        cpp = with pkgs; [
          cmake
          ninja
          pkg-config
          gcc
          gdb
          ccache
          doctest
        ];
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "husk";
          version = "0.1.0";
          src = appSource;

          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
          ];

          cmakeFlags = [ "-DHUSK_BUILD_TESTS=OFF" ];

          installPhase = ''
            mkdir -p $out/bin
            cp husk $out/bin/
          '';
        };

        apps.default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/husk";
        };

        devShells.default = pkgs.mkShell {
          packages = cpp;

          CCACHE_DIR = "/media/luna/cache/ccache";

          shellHook = ''
            echo "husk dev shell"
            echo "  cmake:   $(cmake --version | head -n1)"
            echo "  ccache:  $CCACHE_DIR"
            echo "  doctest: available via find_package(doctest)"
          '';
        };

        formatter = pkgs.nixfmt-rfc-style;
      }
    );
}
