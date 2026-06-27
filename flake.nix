{
  description = "shit — the fastest cross-platform Bash and POSIX-compatible shell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    toiletline = {
      url = "github:toiletbril/toiletline/staging";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, toiletline }:
    let
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = self.packages.${system}.shit;

          shit = let
            mode = "rel";
          in pkgs.stdenv.mkDerivation {
            pname = "shit";
            version = "0.1.0";

            src = self;

            nativeBuildInputs = with pkgs; [
              clang
              gnumake
              git
            ];

            preBuild = ''
              if [ ! -f src/toiletline/toiletline.h ]; then
                echo "providing toiletline submodule from flake input"
                rm -rf src/toiletline
                cp -r ${toiletline} src/toiletline
              fi
            '';

            buildPhase = ''
              runHook preBuild
              make -C src -j$NIX_BUILD_CORES MODE=${mode} CXX=${pkgs.clang}/bin/clang++
              runHook postBuild
            '';

            installPhase = ''
              runHook preInstall
              mkdir -p $out/bin
              cp ./shit $out/bin/
              runHook postInstall
            '';

            meta = with pkgs.lib; {
              description = "The fastest cross-platform Bash and POSIX-compatible shell";
              homepage = "https://github.com/toiletbril/shit";
              license = licenses.mit;
              mainProgram = "shit";
              platforms = platforms.unix;
            };
          };
        }
      );

      devShells = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.shit ];

            packages = with pkgs; [
              clang
              clang-tools
              gnumake
              git
            ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux (with pkgs; [
              mold
              lld
            ]);

            shellHook = ''
              echo "  Build:  make -C src MODE=dbg"
              echo "  Test:   make -C test test"
            '';
          };
        }
      );
    };
}
