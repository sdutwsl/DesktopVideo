{
  description = "Muralop - video desktop wallpaper for Linux Wayland, Windows and macOS";

  # Resolved through the flake registry (system nixpkgs on NixOS); override with
  #   nix develop --override-input nixpkgs github:NixOS/nixpkgs/nixos-unstable
  # Consumers should use `inputs.nixpkgs.follows = "nixpkgs"` so the binary links the
  # same glibc as the system's GPU drivers (/run/opengl-driver).
  inputs.nixpkgs.url = "flake:nixpkgs";

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
      windowsTools =
        pkgs: with pkgs; [
          cmake
          ninja
          _7zz # 7zz unpacks the libmpv SDK (p7zip cannot read the ARM64 archive's filter)
          nsis # makensis, for `ninja installer`
        ];
    in
    {
      packages = forAll (pkgs: rec {
        muralop = pkgs.callPackage ./nix/package.nix { };
        default = muralop;
      });

      overlays.default = final: _prev: {
        muralop = final.callPackage ./nix/package.nix { };
      };

      # programs.muralop.{enable,package,autostart}
      nixosModules.default = import ./nix/nixos-module.nix;
      homeModules.default = import ./nix/home-module.nix;
      homeManagerModules.default = self.homeModules.default;

      devShells = forAll (pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [ self.packages.${pkgs.stdenv.hostPlatform.system}.default ];
        };
        # Cross-compile the Windows exe and its packages: nix develop .#mingw (x64, with
        # cmake/mingw-x86_64.cmake) or .#mingw-arm64 (ARM64, with cmake/mingw-aarch64.cmake).
        mingw = pkgs.mkShell {
          nativeBuildInputs = windowsTools pkgs ++ [ pkgs.pkgsCross.mingwW64.stdenv.cc ];
          buildInputs = [ pkgs.pkgsCross.mingwW64.windows.mcfgthreads ];
        };
        mingw-arm64 = pkgs.mkShell {
          nativeBuildInputs = windowsTools pkgs ++ [ pkgs.pkgsCross.ucrtAarch64.stdenv.cc ];
        };
      });

      checks = forAll (pkgs: {
        package = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
      });
    };
}
