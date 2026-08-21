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
        HUSK_VERSION = "v0.1.1"; # set manually as building on client machine will not have git available to get the version from git describe

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

        # Khronos glTF-Validator: no nixpkgs derivation exists upstream, and
        # no Dart toolchain here to build from source -- pull the official
        # precompiled Linux binary release instead. This is a Dart AOT
        # executable: its actual app logic lives in a VM isolate snapshot
        # appended into the ELF, and BOTH of stdenv's default ELF-mangling
        # fixup steps corrupt that snapshot independently -- confirmed by
        # hand: patchelf --set-interpreter alone (no strip) already breaks
        # it, and separately dontPatchELF alone (leaving the default
        # stripHook active) also breaks it. Only the combination of both
        # dontPatchELF and dontStrip leaves the snapshot intact. Every
        # broken variant "runs" as bare `dart` (e.g. `--help`/`--version`
        # return generic Dart VM output) but dies with "VM initialization
        # failed: Invalid vm isolate snapshot seen" on any real invocation.
        # Fix: leave the ELF completely untouched and run it inside
        # steam-run-free's FHS-compat sandbox instead (same mechanism as
        # steam-run, MIT-licensed, no Steam binary in the closure -- plain
        # steam-run pulls in the unfree steam-unwrapped package and fails
        # eval under this flake's default unfree-disallow policy), which
        # resolves the dynamic linker without ever rewriting the file.
        gltf-validator = pkgs.stdenv.mkDerivation rec {
          pname = "gltf-validator";
          version = "2.0.0-dev.3.10";

          src = pkgs.fetchurl {
            url = "https://github.com/KhronosGroup/glTF-Validator/releases/download/${version}/gltf_validator-${version}-linux64.tar.xz";
            hash = "sha256-Fo66iHlkElq+F66XiZs40LPP1zwmbHhCTBlJKd3LxSI=";
          };
          sourceRoot = ".";
          dontPatchELF = true;
          dontStrip = true;
          nativeBuildInputs = [ pkgs.makeWrapper ];

          # Tarball unpacks flat: gltf_validator, LICENSE, NOTICES, docs/
          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin $out/share/doc/gltf-validator
            install -m755 gltf_validator $out/bin/.gltf_validator-unwrapped
            makeWrapper ${pkgs.steam-run-free}/bin/steam-run $out/bin/gltf_validator \
              --add-flags $out/bin/.gltf_validator-unwrapped
            cp -r LICENSE NOTICES docs $out/share/doc/gltf-validator/
            runHook postInstall
          '';

          meta = with lib; {
            description = "Khronos glTF 2.0 asset validator (precompiled binary)";
            homepage = "https://github.com/KhronosGroup/glTF-Validator";
            license = licenses.asl20;
            platforms = [ "x86_64-linux" ];
            sourceProvenance = with sourceTypes; [ binaryNativeCode ];
          };
        };
        cpp = with pkgs; [
          cmake
          ninja
          pkg-config
          gcc
          gdb
          ccache
          # clang-tools: clang-tidy, for the modernize-*/readability-* lint
          # pass (raw-loop-vs-algorithm, naive-vs-idiomatic patterns) --
          # dev-shell only, gcc stays the actual build compiler.
          clang-tools
          doctest
          tinygltf
          cli11
          casc-tool.packages.${pkgs.system}.default
          gltf-validator
          blender
          sqlite
          # vkd3d-compiler/vkd3d-dxbc: disassemble the captured DXBC shader
          # blobs in references/wow_shaders/ (PIXEL_SHADER_FORMULAS_TODO.md's
          # matching pass) -- dev-shell only, not part of the husk binary.
          vkd3d
          # inotifywait/notify-send: tools/shader_dump_watcher.nu, live-
          # correlates new wow_shader_dump captures with in-game location.
          inotify-tools
          libnotify
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
          src = appSource;
          version = HUSK_VERSION;
          HUSK_ENV_VERSION = HUSK_VERSION;

          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
          ];

          buildInputs = [
            pkgs.tinygltf
            pkgs.cli11
            pkgs.sqlite
          ];

          cmakeFlags = [ "-DHUSK_BUILD_TESTS=OFF" ];

          installPhase = ''
            mkdir -p $out/bin
            cp husk $out/bin/
          '';
        };

        packages.gltf-validator = gltf-validator;

        apps.default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/husk";
        };

        devShells.default = pkgs.mkShell {
          packages = lib.concatLists [
            cpp
            python
          ];

          CCACHE_DIR = "/media/luna/cache/ccache";

          shellHook = ''
                        echo "husk dev shell ${HUSK_VERSION} for ${system}"
                        echo "  cmake:   $(cmake --version | head -n1)"
                        echo "  ccache:  $CCACHE_DIR"
            			echo "  blender: $(blender --version | head -n1)"
            			echo "  gltf-validator: gltf_validator ${gltf-validator.version}"
                        echo "  clang-tidy: $(clang-tidy --version | head -n2 | tail -n1)"
                        echo "  doctest: available via find_package(doctest)"
                        echo "  uv:      $(uv --version) -- cd blp/ && uv sync"
          '';
        };

        formatter = pkgs.nixfmt-rfc-style;
      }
    );
}
