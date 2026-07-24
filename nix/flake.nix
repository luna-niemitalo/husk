{
  description = "husk: M2/M3/WMO -> common-format CLI";

  inputs = {
    pins.url = "path:/home/luna/nix/pins";
    nixpkgs.follows = "pins/nixpkgs";
    flake-utils.url = "github:numtide/flake-utils";
	casc-tool.url = "github:luna-niemitalo/casc-tool?dir=nix";
  };

  outputs =
    inputs@{
      self,
      pins,
      nixpkgs,
      flake-utils,
	  casc-tool,
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
          tinygltf
		  casc-tool.packages.${pkgs.system}.default
        ];

        # blp/: BLP2 -> PNG texture conversion (roadmap stage 4). uv manages
        # its own venv + packages (Pillow, numpy, ...) via blp/pyproject.toml
        # -- deliberately not routed through python3Packages, per Luna's
        # steer to keep Python dependency management on uv rather than
        # nixpkgs' package set for this.
        python = with pkgs; [
          uv
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

          buildInputs = [ pkgs.tinygltf ];

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
          packages = cpp ++ python;

          CCACHE_DIR = "/media/luna/cache/ccache";

          shellHook = ''
            echo "husk dev shell"
            echo "  cmake:   $(cmake --version | head -n1)"
            echo "  ccache:  $CCACHE_DIR"
            echo "  doctest: available via find_package(doctest)"
            echo "  uv:      $(uv --version) -- cd blp/ && uv sync"
          '';
        };

        formatter = pkgs.nixfmt-rfc-style;
      }
    );
}
